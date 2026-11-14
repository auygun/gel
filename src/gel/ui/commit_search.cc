// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/commit_search.h"

#include <algorithm>
#include <string_view>
#include <thread>
#include <unordered_set>

namespace {

// How long the query has to stay the same before the background search of a
// change-based mode starts.
constexpr double kSettleSeconds = 0.3;

// Commits per git diff-tree run. Small enough that a run over the visible
// rows finishes quickly, large enough that process startup is amortized.
constexpr int kChunkSize = 1024;

// git diff-tree processes run at once for the change-based modes, at most.
constexpr unsigned kMaxChunkSlots = 4;

// Time budget per frame for the main-thread kContaining search.
constexpr double kBudgetSeconds = 0.008;

// Wraps |i| into [0, n), including the negative values of reverse walks.
int Wrap(int i, int n) {
  return (i % n + n) % n;
}

// How long the visible rows have to stay put before the background search is
// moved to them, so dragging the scrollbar doesn't start a git process per
// frame.
constexpr double kVisibleSettleSeconds = 0.1;

// Splits |text| on spaces, dropping empty fields.
std::vector<std::string> SplitWords(const std::string& text) {
  std::vector<std::string> words;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find(' ', start);
    if (end == std::string::npos)
      end = text.size();
    if (end > start)
      words.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return words;
}

}  // namespace

bool CommitMatchesSearch(const GitLog::CommitInfo& commit,
                         const RefDecorations::Decoration* decoration,
                         const base::SearchTerm& term) {
  if (base::Contains(commit.author, term) ||
      base::Contains(commit.committer, term)) {
    return true;
  }
  for (const auto& line : commit.message) {
    if (base::Contains(line, term))
      return true;
  }
  if (!decoration)
    return false;
  for (const auto& tag : decoration->tags) {
    if (base::Contains(tag, term))
      return true;
  }
  for (const auto& branch : decoration->branches) {
    if (base::Contains(branch.name, term))
      return true;
  }
  return decoration->is_stash && base::Contains("stash", term);
}

GitChangeSearch::Query BuildChangeSearchQuery(const std::string& term,
                                              CommitSearchMode mode,
                                              base::MatchOptions options) {
  GitChangeSearch::Query query;
  switch (mode) {
    case CommitSearchMode::kContaining:
    case CommitSearchMode::kCount:
      break;

      // Whole word is not offered for these modes: it would need "\b", which
      // not every regex library git can be built with supports.
      //
      // Like git log, merges are not diffed, so they never match.

    case CommitSearchMode::kTouchingPaths:
      // Like gitk, the term is a space-separated list of paths or globs,
      // relative to the current directory as git resolves pathspecs.
      query.pathspecs = SplitWords(term);
      if (!query.pathspecs.empty() && !options.case_sensitive)
        query.git_options.push_back("--icase-pathspecs");
      break;

    case CommitSearchMode::kAddingRemoving:
      // A literal string, as for -S.
      if (!term.empty()) {
        query.diff_options.push_back("-S" + term);
        if (!options.case_sensitive)
          query.diff_options.push_back("--regexp-ignore-case");
      }
      break;

    case CommitSearchMode::kChangingLines:
      // A regex, as for -G.
      if (!term.empty()) {
        query.diff_options.push_back("-G" + term);
        if (!options.case_sensitive)
          query.diff_options.push_back("--regexp-ignore-case");
      }
      break;
  }
  return query;
}

CommitSearch::CommitSearch(GitLog& git_log,
                           RefDecorations& ref_decorations,
                           std::function<void(bool)> busy_callback)
    : git_log_(git_log), ref_decorations_(ref_decorations) {
  // git diff-tree is single-threaded and the change-based modes are bound by
  // it, so run a few at once. Leave cores for the UI and everything else.
  unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  int count = static_cast<int>(std::clamp(cores / 2, 1u, kMaxChunkSlots));
  for (int i = 0; i < count; ++i)
    slots_.push_back(std::make_unique<ChunkSlot>(busy_callback));
}

CommitSearch::~CommitSearch() = default;

void CommitSearch::SetQuery(const std::string& term,
                            CommitSearchMode mode,
                            base::MatchOptions options) {
  if (has_query_ && term == term_text_ && mode == mode_ && options == options_)
    return;
  has_query_ = true;
  term_text_ = term;
  mode_ = mode;
  options_ = options;

  // Matches of the previous query are dropped right away so the rows don't
  // show stale highlights.
  Cancel();
  ResetResults();
  if (is_change_mode()) {
    term_ = {};
    query_ = BuildChangeSearchQuery(term_text_, mode_, options_);
    active_ = !query_.empty();
  } else {
    term_ = base::SearchTerm(term_text_, options_);
    query_ = {};
    active_ = !term_.empty();
    // Cheap enough to highlight the rows on screen without waiting.
    CheckVisibleContaining();
  }
  // Wait for the query to settle before searching the rest of the list.
  settle_timer_ = {};
  settle_pending_ = active_;
}

void CommitSearch::ApplyQuery() {
  settle_pending_ = false;
  if (!active_ || scan_enabled_)
    return;
  scan_enabled_ = true;
  RestartScan(visible_first_ >= 0 ? visible_first_ : 0, 1);
}

void CommitSearch::ResetResults() {
  StopAllChunks();
  states_.assign(states_.size(), State::kUnknown);
  known_ = 0;
  scan_enabled_ = false;
  scan_walked_ = 0;
}

void CommitSearch::OnCommitsCleared() {
  Cancel();
  states_.clear();
  ResetResults();
  // The query is still the same: search the reloaded list without waiting.
  if (active_ && !settle_pending_) {
    scan_enabled_ = true;
    RestartScan(0, 1);
  }
}

void CommitSearch::SyncStateSize(int n) {
  // New commits were loaded. The scan picks them up when it gets to them.
  if (static_cast<int>(states_.size()) < n)
    states_.resize(n, State::kUnknown);
}

void CommitSearch::SetState(int index, State state) {
  if (index < 0 || index >= static_cast<int>(states_.size()))
    return;
  if (!IsKnown(states_[index]) && IsKnown(state))
    ++known_;
  else if (IsKnown(states_[index]) && !IsKnown(state))
    --known_;
  states_[index] = state;
}

void CommitSearch::SetVisibleRange(int first, int last) {
  if (first != visible_first_ || last != visible_last_) {
    visible_first_ = first;
    visible_last_ = last;
    visible_timer_ = {};
    visible_pending_ = true;
  }
}

void CommitSearch::CheckVisibleContaining() {
  if (!active_ || visible_first_ < 0)
    return;
  auto commits = git_log_.GetCommits();
  int n = static_cast<int>(commits.size());
  SyncStateSize(n);
  for (int i = visible_first_; i <= visible_last_ && i < n; ++i) {
    if (states_[i] != State::kUnknown)
      continue;
    bool match = CommitMatchesSearch(
        commits[i], ref_decorations_.Find(commits[i].commit), term_);
    SetState(i, match ? State::kMatch : State::kNoMatch);
  }
}

void CommitSearch::ApplyLabelChanges() {
  ref_decorations_.ConsumeChangedObjects(changed_objects_);
  if (changed_objects_.empty())
    return;
  if (is_change_mode() || known_ == 0) {
    changed_objects_.clear();
    return;
  }
  // Tag and branch names are searched too, so the commits whose labels
  // changed are checked again. Labels change in bursts (a ref moved, the
  // labels loading), so one pass over the list per burst finds them.
  std::unordered_set<std::string_view> changed(changed_objects_.begin(),
                                               changed_objects_.end());
  auto commits = git_log_.GetCommits();
  int n = std::min(static_cast<int>(commits.size()),
                   static_cast<int>(states_.size()));
  for (int i = 0; i < n; ++i) {
    if (states_[i] != State::kUnknown && changed.contains(commits[i].commit)) {
      SetState(i, State::kUnknown);
      // Let a scan that already went past it come back for it.
      scan_walked_ = 0;
    }
  }
  changed_objects_.clear();
  // Changed rows on screen are highlighted right away.
  CheckVisibleContaining();
}

void CommitSearch::PrioritizeVisible() {
  if (!visible_pending_ || visible_timer_.Elapsed() < kVisibleSettleSeconds)
    return;
  visible_pending_ = false;
  // Stepping to a match decides where to search itself.
  if (!scan_enabled_ || searching_ || visible_first_ < 0)
    return;
  // Check the newly visible rows first, unless they are already known.
  int n = static_cast<int>(states_.size());
  for (int i = visible_first_; i <= visible_last_ && i < n; ++i) {
    if (states_[i] == State::kUnknown) {
      EnsureChecked(i, 1);
      return;
    }
  }
}

void CommitSearch::Search(int start, int direction) {
  start_ = start;
  direction_ = direction;
  search_offset_ = 0;
  result_ = -1;

  // A query whose search failed in the background is run again, so the
  // failure is reported this time.
  if (!active_ && is_change_mode() && !query_.empty()) {
    active_ = true;
    ResetResults();
  }
  // Don't wait for the query to settle when asked to search.
  ApplyQuery();
  searching_ = active_ && !git_log_.GetCommits().empty();
}

bool CommitSearch::Update() {
  int n = static_cast<int>(git_log_.GetCommits().size());
  SyncStateSize(n);
  if (!is_change_mode()) {
    ApplyLabelChanges();
    // Rows scrolled into view are cheap to check right away.
    CheckVisibleContaining();
  }

  if (settle_pending_ && settle_timer_.Elapsed() >= kSettleSeconds)
    ApplyQuery();

  if (is_change_mode())
    ProcessEvents();
  if (searching_)
    UpdateNavigation();
  else
    PrioritizeVisible();
  if (scan_enabled_)
    CheckMore();
  // Stepping may have reached the commits just checked.
  if (searching_)
    UpdateNavigation();
  return searching_;
}

bool CommitSearch::needs_frames() const {
  bool unchecked = known_ < static_cast<int>(states_.size());
  return settle_pending_ || (visible_pending_ && scan_enabled_) ||
         (!is_change_mode() && scan_enabled_ && unchecked);
}

void CommitSearch::CheckMore() {
  if (is_change_mode())
    FillSlots();
  else
    CheckMoreContaining();
}

void CommitSearch::CheckMoreContaining() {
  auto commits = git_log_.GetCommits();
  int n = static_cast<int>(commits.size());
  // The list may have grown since the last frame (or been reloaded).
  SyncStateSize(n);
  if (n == 0 || known_ >= n)
    return;
  if (scan_walked_ >= n)
    scan_walked_ = 0;

  base::ElapsedTimer timer;
  int cursor = Wrap(scan_cursor_, n);
  int checked = 0;
  while (scan_walked_ < n) {
    if (states_[cursor] == State::kUnknown) {
      bool match = CommitMatchesSearch(
          commits[cursor], ref_decorations_.Find(commits[cursor].commit),
          term_);
      SetState(cursor, match ? State::kMatch : State::kNoMatch);
      // Check the time budget every 1024 commits to amortize the clock call.
      if ((++checked & 1023) == 0 && timer.Elapsed() >= kBudgetSeconds) {
        cursor = Wrap(cursor + scan_direction_, n);
        ++scan_walked_;
        break;
      }
      // Hand a match to stepping right away rather than finish the slice.
      if (match && searching_) {
        cursor = Wrap(cursor + scan_direction_, n);
        ++scan_walked_;
        break;
      }
    }
    cursor = Wrap(cursor + scan_direction_, n);
    ++scan_walked_;
  }
  scan_cursor_ = cursor;
}

void CommitSearch::UpdateNavigation() {
  int n = static_cast<int>(states_.size());
  if (n == 0 || !active_) {
    FinishNavigation(-1);
    return;
  }

  int begin =
      start_ < 0 ? (direction_ > 0 ? 0 : n - 1) : Wrap(start_ + direction_, n);
  // Walk the known results up to the first unchecked commit.
  while (search_offset_ < n) {
    int i = Wrap(begin + direction_ * search_offset_, n);
    State state = states_[i];
    if (state == State::kMatch) {
      FinishNavigation(i);
      return;
    }
    if (!IsKnown(state)) {
      // Never walk past a commit whose result is not known: a chunk further
      // along may finish first, but the next match can only be here or
      // later in the walk.
      EnsureChecked(i, direction_);
      return;
    }
    search_offset_++;
  }
  FinishNavigation(-1);
}

void CommitSearch::FinishNavigation(int result) {
  result_ = result;
  searching_ = false;
}

void CommitSearch::EnsureChecked(int index, int direction) {
  // Fine if a running chunk is about to check it.
  for (const auto& slot : slots_) {
    if (!slot->running)
      continue;
    auto begin = slot->chunk.begin() + slot->done;
    auto it = std::find(begin, slot->chunk.end(), index);
    if (it != slot->chunk.end() && it - begin < kChunkSize / 2)
      return;
  }
  // Fine if the scan continues right there and nothing is running ahead of
  // it, which happens in Containing mode and between chunks.
  if (!any_chunk_running() && scan_cursor_ == index &&
      scan_direction_ == direction &&
      scan_walked_ < static_cast<int>(states_.size())) {
    return;
  }
  RestartScan(index, direction);
}

void CommitSearch::RestartScan(int index, int direction) {
  // Commits the stopped chunks didn't get to are unchecked again; restarting
  // the walk from |index| picks them up. What they finished stays known.
  StopAllChunks();
  scan_cursor_ = index;
  scan_direction_ = direction;
  scan_walked_ = 0;
  CheckMore();
}

void CommitSearch::StopChunk(ChunkSlot& slot) {
  // Superseding the run id drops events of a run still in flight.
  slot.run_id = ++next_run_id_;
  if (slot.running)
    slot.search.Kill();
  // Commits it didn't get to are up for grabs again.
  for (size_t p = slot.done; p < slot.chunk.size(); ++p) {
    int ci = slot.chunk[p];
    if (ci < static_cast<int>(states_.size()) &&
        states_[ci] == State::kChecking) {
      SetState(ci, State::kUnknown);
    }
  }
  slot.running = false;
  slot.chunk.clear();
  slot.done = 0;
}

void CommitSearch::StopAllChunks() {
  for (auto& slot : slots_)
    StopChunk(*slot);
}

bool CommitSearch::any_chunk_running() const {
  for (const auto& slot : slots_) {
    if (slot->running)
      return true;
  }
  return false;
}

void CommitSearch::FillSlots() {
  for (auto& slot : slots_) {
    if (!slot->running && (!active_ || !StartChunk(*slot)))
      break;
  }
}

bool CommitSearch::StartChunk(ChunkSlot& slot) {
  auto commits = git_log_.GetCommits();
  int n = static_cast<int>(commits.size());
  SyncStateSize(n);
  if (n == 0 || known_ >= n)
    return false;

  // Collect the next unchecked commits that no other chunk holds, wrapping
  // around once. The walk only moves forward, so chunks never overlap.
  // Commits loaded after the walk covered the list start a new walk.
  int walked = scan_walked_ >= n ? 0 : scan_walked_;
  int cursor = Wrap(scan_cursor_, n);
  std::vector<int>& chunk = slot.chunk;
  chunk.clear();
  while (walked < n && static_cast<int>(chunk.size()) < kChunkSize) {
    if (states_[cursor] == State::kUnknown)
      chunk.push_back(cursor);
    cursor = Wrap(cursor + scan_direction_, n);
    ++walked;
  }
  // While the log is still loading, wait for a full chunk rather than start a
  // process per frame for the few commits that arrived. The first chunk and
  // stepping to a match don't wait, since someone is looking at those.
  bool partial = static_cast<int>(chunk.size()) < kChunkSize;
  if (chunk.empty() ||
      (partial && git_log_.busy() && known_ > 0 && !searching_)) {
    chunk.clear();
    return false;
  }
  scan_cursor_ = cursor;
  scan_walked_ = walked;

  slot.done = 0;
  slot.events_seen = 0;
  slot.run_id = ++next_run_id_;
  slot.running = slot.search.Run(slot.run_id, query_, commits, chunk);
  if (slot.running) {
    for (int ci : chunk)
      SetState(ci, State::kChecking);
  }
  if (!slot.running) {
    // git could not be started at all.
    active_ = false;
    scan_enabled_ = false;
    StopAllChunks();
    return false;
  }
  return true;
}

void CommitSearch::ProcessEvents() {
  for (auto& slot : slots_)
    ProcessSlotEvents(*slot);
}

void CommitSearch::ProcessSlotEvents(ChunkSlot& slot) {
  slot.search.Update();
  if (slot.search.DidClear())
    slot.events_seen = 0;
  auto events = slot.search.GetEvents();
  for (; slot.events_seen < events.size(); ++slot.events_seen) {
    const auto& event = events[slot.events_seen];
    if (event.run_id != slot.run_id || !slot.running)
      continue;
    std::vector<int>& chunk = slot.chunk;
    switch (event.kind) {
      case GitChangeSearch::Event::Kind::kMatch: {
        // The match is one of the commits after the last marker. Search them
        // for it rather than trusting the order, which diff-tree keeps but
        // doesn't promise.
        int from = std::clamp(event.position, 0, (int)chunk.size());
        int to = std::min(from + GitChangeSearch::kMarkerInterval,
                          (int)chunk.size());
        auto commits = git_log_.GetCommits();
        for (int p = from; p < to; ++p) {
          int ci = chunk[p];
          if (ci < static_cast<int>(commits.size()) &&
              commits[ci].commit == event.text) {
            SetState(ci, State::kMatch);
            break;
          }
        }
        break;
      }
      case GitChangeSearch::Event::Kind::kProgress: {
        // Everything before the marker is checked. Whatever did not match
        // doesn't: diff-tree prints a commit's match before the marker that
        // follows it, so no match can still be on its way.
        int to = std::clamp(event.position, 0, (int)chunk.size());
        for (int p = slot.done; p < to; ++p) {
          int ci = chunk[p];
          if (ci < static_cast<int>(states_.size()) && !IsKnown(states_[ci])) {
            SetState(ci, State::kNoMatch);
          }
        }
        slot.done = std::max(slot.done, to);
        break;
      }
      case GitChangeSearch::Event::Kind::kFinished:
        if (!event.failed) {
          // The last marker settles every commit; anything still held means
          // git stopped short of it. Hand those out again rather than leave
          // stepping waiting for them.
          for (size_t p = slot.done; p < chunk.size(); ++p) {
            int ci = chunk[p];
            if (ci < static_cast<int>(states_.size()) &&
                states_[ci] == State::kChecking) {
              SetState(ci, State::kUnknown);
              scan_walked_ = 0;
            }
          }
        }
        slot.running = false;
        if (event.failed) {
          // Nothing more can be found for this query (e.g. invalid regex).
          // The other chunks fail the same way: stop them and report once.
          active_ = false;
          scan_enabled_ = false;
          StopAllChunks();
          if (searching_) {
            error_ = event.text.empty() ? "git diff-tree failed." : event.text;
            has_error_ = true;
            FinishNavigation(-1);
          }
          return;
        }
        break;
    }
  }
}

void CommitSearch::Cancel() {
  searching_ = false;
}

bool CommitSearch::IsMatch(int index) const {
  return index >= 0 && index < static_cast<int>(states_.size()) &&
         states_[index] == State::kMatch;
}

int CommitSearch::ConsumeResult() {
  int r = result_;
  result_ = -1;
  return r;
}

bool CommitSearch::ConsumeError(std::string& error) {
  if (!has_error_)
    return false;
  has_error_ = false;
  error = std::move(error_);
  return true;
}

float CommitSearch::progress() const {
  // One measure for stepping to a match and for checking the list: how much
  // of it has been checked. It only grows, so the bar doesn't jump back when
  // a step starts mid-scan.
  int n = static_cast<int>(states_.size());
  bool checking = scan_enabled_ && (any_chunk_running() || !is_change_mode());
  if (!(searching_ || checking) || n == 0 || known_ >= n)
    return -1.0f;
  return static_cast<float>(known_) / n;
}
