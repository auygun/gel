// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/commit_search.h"

#include <algorithm>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

namespace {

// How long the query has to stay the same before the background search of a
// change-based mode starts.
constexpr double kSettleSeconds = 0.3;

// How much work each git diff-tree process is given ahead, in seconds of
// checking at the rate it has been going. Enough to keep it busy between
// frames, little enough that the search changes course soon after stepping
// or scrolling: commits already given to git can't be taken back.
constexpr double kQueueAheadSeconds = 0.25;

// Bounds on the commits each process is given ahead.
constexpr int kMinQueued = 2;
constexpr int kMaxQueued = 256;

// Floor for the measured time per commit, so a run of instant results
// doesn't make it hand out an unbounded amount at once.
constexpr double kMinSecondsPerCommit = 0.001;

// What a process assumes before it has measured anything.
constexpr double kInitialSecondsPerCommit = 0.05;

// git diff-tree processes run at once for the change-based modes, at most.
constexpr unsigned kMaxSearchProcesses = 4;

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

// Escapes the POSIX extended regex metacharacters in |text|, so the regex
// matches it literally. ']' and '}' are literal on their own; a backslash in
// front of them is undefined in POSIX, so they are left alone.
std::string EscapeExtendedRegex(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size() * 2);
  for (char c : text) {
    if (std::string_view(".[\\()*+?{|^$").find(c) != std::string_view::npos)
      escaped += '\\';
    escaped += c;
  }
  return escaped;
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
                                              base::MatchOptions options,
                                              bool regex) {
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
      // -S takes a literal string, or a regex with --pickaxe-regex.
      if (!term.empty()) {
        query.diff_options.push_back("-S" + term);
        if (regex)
          query.diff_options.push_back("--pickaxe-regex");
      }
      break;

    case CommitSearchMode::kChangingLines:
      // -G takes a regex. git has no fixed-string -G, so a literal term is
      // escaped into a regex matching it.
      if (!term.empty())
        query.diff_options.push_back(
            "-G" + (regex ? term : EscapeExtendedRegex(term)));
      break;
  }
  if (!query.diff_options.empty()) {
    if (!options.case_sensitive)
      query.diff_options.push_back("--regexp-ignore-case");
    // Only whether a commit matches is needed, not which of its files do, so
    // let git stop at the first matching file. By default it checks every
    // one, which takes several times longer for commits touching many files.
    query.diff_options.push_back("--pickaxe-all");
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
  int count = static_cast<int>(std::clamp(cores / 2, 1u, kMaxSearchProcesses));
  for (int i = 0; i < count; ++i)
    slots_.push_back(std::make_unique<SearchProcess>(busy_callback));
}

CommitSearch::~CommitSearch() = default;

void CommitSearch::SetQuery(const std::string& term,
                            CommitSearchMode mode,
                            base::MatchOptions options,
                            bool regex) {
  if (has_query_ && term == term_text_ && mode == mode_ &&
      options == options_ && regex == regex_)
    return;
  has_query_ = true;
  term_text_ = term;
  mode_ = mode;
  options_ = options;
  regex_ = regex;

  // Matches of the previous query are dropped right away so the rows don't
  // show stale highlights.
  Cancel();
  ResetResults();
  if (is_change_mode()) {
    term_ = {};
    query_ = BuildChangeSearchQuery(term_text_, mode_, options_, regex_);
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
  StopAllSlots();
  states_.assign(states_.size(), State::kUnknown);
  known_ = 0;
  matches_ = 0;
  ++match_generation_;
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
  if ((states_[index] == State::kMatch) != (state == State::kMatch)) {
    matches_ += state == State::kMatch ? 1 : -1;
    ++match_generation_;
  }
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
  step_may_clear_ = true;

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
      // Never walk past a commit whose result is not known: a commit further
      // along may finish first, but the next match can only be here or
      // later in the walk.
      if (step_may_clear_ && ClearPathForStep(i, direction_)) {
        // The stopped processes take the step's commits next.
        step_may_clear_ = false;
        RestartScan(i, direction_);
        return;
      }
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
  // Fine if it is with git already, or the scan continues right there.
  if (index < static_cast<int>(states_.size()) &&
      states_[index] == State::kChecking) {
    return;
  }
  if (scan_cursor_ == index && scan_direction_ == direction &&
      scan_walked_ < static_cast<int>(states_.size())) {
    return;
  }
  RestartScan(index, direction);
}

bool CommitSearch::ClearPathForStep(int index, int direction) {
  // Fine if git is already checking it.
  for (const auto& slot : slots_) {
    if (slot->running && !slot->queued.empty() &&
        slot->queued.front() == index) {
      return false;
    }
  }
  // The commits the step needs next: the first few unknown ones from |index|
  // on, one per process.
  int n = static_cast<int>(states_.size());
  std::vector<int> needed;
  for (int walked = 0; walked < n && needed.size() < slots_.size(); ++walked) {
    int ci = Wrap(index + direction * walked, n);
    if (!IsKnown(states_[ci]))
      needed.push_back(ci);
  }
  // A process busy with a commit the step doesn't need next is in its way,
  // along with whatever was queued behind that commit for the search's old
  // position: stop it, losing the commit it is in the middle of, and let the
  // scan hand it the step's commits instead. Only stepping does this; the
  // user is waiting for its answer.
  bool stopped = false;
  for (auto& slot : slots_) {
    if (slot->running && !slot->queued.empty() &&
        std::find(needed.begin(), needed.end(), slot->queued.front()) ==
            needed.end()) {
      StopSlot(*slot);
      stopped = true;
    }
  }
  return stopped;
}

void CommitSearch::RestartScan(int index, int direction) {
  // The commits already with git are checked as they are, wherever the scan
  // goes: they come back soon, and are known from then on. Only what is
  // handed out next changes.
  scan_cursor_ = index;
  scan_direction_ = direction;
  scan_walked_ = 0;
  CheckMore();
}

void CommitSearch::StopSlot(SearchProcess& slot) {
  // Superseding the run id drops events of a run still in flight.
  slot.run_id = ++next_run_id_;
  if (slot.running)
    slot.search.Kill();
  // Commits it didn't get to are up for grabs again. Let a scan that already
  // went past them come back for them.
  for (int ci : slot.queued) {
    if (ci < static_cast<int>(states_.size()) &&
        states_[ci] == State::kChecking) {
      SetState(ci, State::kUnknown);
      scan_walked_ = 0;
    }
  }
  slot.queued.clear();
  slot.running = false;
  slot.finishing = false;
  slot.search.DropUnsent();
  slot.search.ClearEvents();
}

void CommitSearch::StopAllSlots() {
  for (auto& slot : slots_)
    StopSlot(*slot);
}

bool CommitSearch::any_slot_running() const {
  for (const auto& slot : slots_) {
    if (slot->running)
      return true;
  }
  return false;
}

int CommitSearch::NextUnchecked() {
  int n = static_cast<int>(states_.size());
  // Commits loaded after the walk covered the list start a new walk.
  if (scan_walked_ >= n)
    scan_walked_ = 0;
  while (scan_walked_ < n) {
    int ci = Wrap(scan_cursor_, n);
    scan_cursor_ = Wrap(ci + scan_direction_, n);
    ++scan_walked_;
    if (states_[ci] == State::kUnknown)
      return ci;
  }
  return -1;
}

void CommitSearch::FillSlots() {
  if (!active_)
    return;
  auto commits = git_log_.GetCommits();
  int n = static_cast<int>(commits.size());
  SyncStateSize(n);
  if (n == 0)
    return;

  // Hand the next commits out in turn, one at a time, to whichever process
  // has the least work ahead, so all of them work through the same stretch
  // of the list side by side: the commits nearest the scan start (the
  // visible rows, or where stepping to a match is) are checked first, by all
  // of them, and a run of slow commits is shared out.
  //
  // Near the end, a process is given no more than its share of what is left
  // (once the log is loaded): commits given to git can't be taken back, so
  // one that got through its own quickly would otherwise sit idle while
  // another works through a queue of slow commits.
  int slot_count = static_cast<int>(slots_.size());
  int checking = 0;
  for (const auto& slot : slots_)
    checking += static_cast<int>(slot->queued.size());
  int unknown = n - known_ - checking;
  bool out_of_commits = unknown <= 0;
  while (!out_of_commits) {
    int share =
        git_log_.busy() ? kMaxQueued : std::max(1, unknown / slot_count);
    SearchProcess* best = nullptr;
    double best_ahead = 0;
    for (auto& slot : slots_) {
      if (slot->finishing)
        continue;
      int queued = static_cast<int>(slot->queued.size());
      int target = std::clamp(
          static_cast<int>(kQueueAheadSeconds / slot->seconds_per_commit),
          kMinQueued, kMaxQueued);
      target = std::max(1, std::min(target, share + 1));
      if (queued >= target)
        continue;
      double ahead = queued * slot->seconds_per_commit;
      if (!best || ahead < best_ahead) {
        best = slot.get();
        best_ahead = ahead;
      }
    }
    if (!best)
      break;
    int ci = NextUnchecked();
    if (ci < 0) {
      out_of_commits = true;
      break;
    }
    if (!best->running && !StartSlot(*best))
      return;
    if (best->queued.empty())
      best->check_timer = {};
    best->queued.push_back(ci);
    best->search.Check(ci, commits[ci].commit);
    SetState(ci, State::kChecking);
    --unknown;
  }
  for (auto& slot : slots_) {
    if (slot->running)
      slot->search.Flush();
  }

  // Once every commit is with git or known, and no more are coming, let the
  // processes end when they are done, so nothing keeps running for nothing.
  // New commits loaded or the scan coming back for a commit restart them.
  if (out_of_commits && !git_log_.busy()) {
    for (auto& slot : slots_) {
      if (slot->running && !slot->finishing) {
        slot->finishing = true;
        slot->search.Finish();
      }
    }
  }
}

bool CommitSearch::StartSlot(SearchProcess& slot) {
  slot.run_id = ++next_run_id_;
  slot.queued.clear();
  slot.finishing = false;
  // A new process may be for a new query, which can be far slower or faster
  // than the last one. Its first commit, which includes starting git, isn't
  // measured.
  slot.seconds_per_commit = kInitialSecondsPerCommit;
  slot.timed = false;
  slot.search.DropUnsent();
  slot.search.ClearEvents();
  slot.events_seen = 0;
  slot.running = slot.search.Start(slot.run_id, query_);
  if (!slot.running) {
    // git could not be started at all.
    active_ = false;
    scan_enabled_ = false;
    StopAllSlots();
    return false;
  }
  return true;
}

void CommitSearch::ProcessEvents() {
  for (auto& slot : slots_)
    ProcessSlotEvents(*slot);
}

void CommitSearch::ProcessSlotEvents(SearchProcess& slot) {
  slot.search.Update();
  auto events = slot.search.GetEvents();
  for (; slot.events_seen < events.size(); ++slot.events_seen) {
    const auto& event = events[slot.events_seen];
    if (event.run_id != slot.run_id || !slot.running)
      continue;
    switch (event.kind) {
      case GitChangeSearch::Event::Kind::kChecked: {
        // Commits come back in the order they were passed.
        if (slot.queued.empty() || slot.queued.front() != event.index)
          break;
        slot.queued.pop_front();
        // How long checking a commit takes, smoothed, to size the work it
        // is given ahead. Measured from when the commit got to the front.
        double seconds = slot.check_timer.Elapsed();
        slot.check_timer = {};
        if (std::exchange(slot.timed, true)) {
          slot.seconds_per_commit =
              std::max(kMinSecondsPerCommit,
                       slot.seconds_per_commit * 0.75 + seconds * 0.25);
        }
        if (event.index < static_cast<int>(states_.size()) &&
            states_[event.index] == State::kChecking) {
          SetState(event.index, event.match ? State::kMatch : State::kNoMatch);
        }
        break;
      }
      case GitChangeSearch::Event::Kind::kFinished: {
        // Anything still queued means git stopped short of it (or was told
        // to finish early). Hand those out again rather than leave stepping
        // waiting for them.
        for (int ci : slot.queued) {
          if (ci < static_cast<int>(states_.size()) &&
              states_[ci] == State::kChecking) {
            SetState(ci, State::kUnknown);
            scan_walked_ = 0;
          }
        }
        slot.queued.clear();
        slot.running = false;
        slot.finishing = false;
        if (event.failed) {
          // Nothing more can be found for this query (e.g. invalid regex).
          // The other processes fail the same way: stop them and report once.
          active_ = false;
          scan_enabled_ = false;
          StopAllSlots();
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
  // Events are only needed once; dropping them keeps the buffer small over a
  // long search.
  if (slot.events_seen > 0 && slot.events_seen == events.size()) {
    slot.search.ClearEvents();
    slot.events_seen = 0;
  }
}

void CommitSearch::Cancel() {
  searching_ = false;
}

bool CommitSearch::IsMatch(int index) const {
  return index >= 0 && index < static_cast<int>(states_.size()) &&
         states_[index] == State::kMatch;
}

CommitSearchRowState CommitSearch::GetRowState(int index) const {
  if (index < 0 || index >= static_cast<int>(states_.size()))
    return CommitSearchRowState::kNone;
  switch (states_[index]) {
    case State::kMatch:
      return CommitSearchRowState::kMatch;
    case State::kChecking:
      // Each process checks its commits in order, so the front of its queue
      // is the one git is working on; the rest wait behind it.
      for (const auto& slot : slots_) {
        if (slot->running && !slot->queued.empty() &&
            slot->queued.front() == index) {
          return CommitSearchRowState::kChecking;
        }
      }
      return CommitSearchRowState::kPending;
    case State::kUnknown:
      // Not handed to git yet, but it will be while the search runs: it goes
      // on until every commit is checked. Not before it starts, so rows don't
      // flash while typing, nor once git has failed on the query. kContaining
      // checks too fast to be worth marking.
      if (is_change_mode() && scan_enabled_)
        return CommitSearchRowState::kPending;
      break;
    case State::kNoMatch:
      break;
  }
  return CommitSearchRowState::kNone;
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

int CommitSearch::MatchRank(int index) const {
  if (!IsMatch(index))
    return -1;
  // Counting is linear in the list, so only redo it when the selection or the
  // matches changed.
  if (index != rank_index_ || match_generation_ != rank_generation_) {
    rank_index_ = index;
    rank_generation_ = match_generation_;
    rank_ = static_cast<int>(
        std::count(states_.begin(), states_.begin() + index, State::kMatch));
  }
  return rank_;
}

bool CommitSearch::search_complete() const {
  if (!active_)
    return true;  // Nothing to search for, or git failed on the query.
  return !settle_pending_ && scan_enabled_ && !git_log_.busy() &&
         known_ >= static_cast<int>(git_log_.GetCommits().size());
}

float CommitSearch::progress() const {
  // One measure for stepping to a match and for checking the list: how much
  // of it has been checked. It only grows, so the bar doesn't jump back when
  // a step starts mid-scan.
  int n = static_cast<int>(states_.size());
  bool checking = scan_enabled_ && (any_slot_running() || !is_change_mode());
  if (!(searching_ || checking) || n == 0 || known_ >= n)
    return -1.0f;
  return static_cast<float>(known_) / n;
}
