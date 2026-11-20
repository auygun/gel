// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_COMMIT_SEARCH_H
#define GEL_UI_COMMIT_SEARCH_H

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/text_search.h"
#include "gel/commands/git_change_search.h"
#include "gel/commands/git_log.h"
#include "gel/ref_decorations.h"
#include "gel/ui/commit_search_mode.h"
#include "third_party/kaliber/base/timer.h"

// Returns true if any of |commit|'s searchable fields contain |term|: author,
// committer, message, and the tag names, branch names and "stash" label in
// |decoration| (may be null). Callers scanning many commits should build the
// term once and pass it in.
bool CommitMatchesSearch(const GitLog::CommitInfo& commit,
                         const RefDecorations::Decoration* decoration,
                         const base::SearchTerm& term);

// Builds the diff-tree query for a change-based search |mode|. Returns an
// empty query for kContaining or a term with nothing to search for. |regex|
// makes kAddingRemoving and kChangingLines match the term as a regex, not a
// literal string.
GitChangeSearch::Query BuildChangeSearchQuery(const std::string& term,
                                              CommitSearchMode mode,
                                              base::MatchOptions options,
                                              bool regex);

// The toolbar commit search: finds the commits matching a term, to highlight
// them in the commit history and to step through them.
//
// Every mode works the same way. What is known about each commit (match, no
// match, not checked yet) is remembered for the current query. The visible
// commits are checked first, then, once the query has stayed the same for a
// moment, the rest of the list, so the rows on screen are highlighted as soon
// as possible. Stepping to the next match walks the remembered results, moving
// the search to wherever the walk reaches unchecked commits.
//
// Only how commits are checked differs:
// - kContaining matches commit fields (see CommitMatchesSearch) on the main
//   thread, within a time budget per frame so the UI stays responsive even for
//   very large histories. It is cheap enough to check the visible rows on the
//   frame the query changes.
// - The other modes match what a commit changed, which only git can tell.
//   GitChangeSearch checks them in the background, in a few long-running
//   git processes.
class CommitSearch {
 public:
  CommitSearch(GitLog& git_log,
               RefDecorations& ref_decorations,
               std::function<void(bool)> busy_callback);
  ~CommitSearch();

  CommitSearch(CommitSearch const&) = delete;
  CommitSearch& operator=(CommitSearch const&) = delete;

  // Sets what to search for. Meant to be called every frame with the current
  // toolbar state; a change starts over. Searching beyond the visible rows
  // starts once the query has stayed the same for a moment, so typing does not
  // start a search of the whole list (or a git process) per keystroke.
  // |regex| applies to kAddingRemoving and kChangingLines; see
  // BuildChangeSearchQuery().
  void SetQuery(const std::string& term,
                CommitSearchMode mode,
                base::MatchOptions options,
                bool regex);

  // Starts stepping from commit index |start| (-1 for none) to the next match
  // in |direction| (1 or -1), wrapping around. The result is picked up with
  // ConsumeResult().
  void Search(int start, int direction);

  // Called each frame. Does a slice of search work if needed.
  // Returns true while stepping to a match is still in progress (caller
  // should keep rendering frames).
  bool Update();

  // Returns true while there is work that nothing else would wake the caller
  // up for: waiting for the query or the visible rows to settle, or checking
  // commits on the main thread. The caller should keep rendering frames.
  bool needs_frames() const;

  // Cancels stepping to a match (if any). Matches already found are kept.
  void Cancel();

  // The commit list was cleared and is being reloaded: forgets every match.
  void OnCommitsCleared();

  // The range of commit indices visible in the commit history, [first, last].
  // These are checked first.
  void SetVisibleRange(int first, int last);

  // Returns true if the commit at |index| is known to match, for highlighting.
  bool IsMatch(int index) const;

  // What is known about the commit at |index|, for drawing its row: whether
  // it matches, or, in the change-based modes, whether it is still waiting to
  // be checked or being checked by a git process.
  CommitSearchRowState GetRowState(int index) const;

  // Returns and clears a pending result index, or -1 if none.
  int ConsumeResult();

  // Returns true once, moving the message into |error|, if git failed while
  // stepping to a match (e.g. an invalid regex). Failures of the background
  // search alone are kept quiet, since they would interrupt typing.
  bool ConsumeError(std::string& error);

  // Returns search progress: -1 = idle, [0,1] = in progress. The share of
  // commits checked, while stepping to a match or checking the list.
  float progress() const;

  // Commits known to match the current query so far.
  int match_count() const { return matches_; }

  // Returns the 0-based position of the commit at |index| among the matches
  // in list order, or -1 if it is not known to match.
  int MatchRank(int index) const;

  // Returns true once every loaded commit has been checked against the
  // current query and the log has finished loading, so match_count() is the
  // final count.
  bool search_complete() const;

  CommitSearchMode mode() const { return mode_; }

 private:
  // kChecking: handed to a git process, result not known yet. Kept apart
  // from kUnknown so that a commit is never handed to two processes.
  enum class State : uint8_t { kUnknown, kChecking, kMatch, kNoMatch };
  static bool IsKnown(State state) {
    return state == State::kMatch || state == State::kNoMatch;
  }

  GitLog& git_log_;
  RefDecorations& ref_decorations_;

  // Current query.
  std::string term_text_;
  CommitSearchMode mode_ = CommitSearchMode::kContaining;
  base::MatchOptions options_;
  bool regex_ = false;
  bool has_query_ = false;
  base::SearchTerm term_;         // kContaining.
  GitChangeSearch::Query query_;  // Change-based modes.
  // Whether the query has anything to search for. Cleared when git fails on
  // it, as nothing more can be found.
  bool active_ = false;
  base::ElapsedTimer settle_timer_;
  bool settle_pending_ = false;

  // Stepping to a match.
  int start_ = -1;
  int direction_ = 1;
  int search_offset_ = 0;  // How many commits walked so far.
  bool searching_ = false;
  // Whether the step may still stop processes in its way. Once per step, so
  // a step waiting on slow commits doesn't keep restarting them, but kept
  // until it does stop some.
  bool step_may_clear_ = false;
  int result_ = -1;  // Pending result for the caller.
  std::string error_;
  bool has_error_ = false;

  // What is known about each commit, by index.
  std::vector<State> states_;
  int known_ = 0;
  int matches_ = 0;
  // Bumped whenever a commit becomes or stops being a match, to invalidate
  // the MatchRank() cache.
  uint64_t match_generation_ = 0;
  mutable int rank_index_ = -1;
  mutable int rank_ = -1;
  mutable uint64_t rank_generation_ = 0;

  // Checking commits beyond the visible rows. Commits are taken from
  // |scan_cursor_| on in |scan_direction_|, wrapping around, until
  // |scan_walked_| covers the list. Disabled until the query settles.
  bool scan_enabled_ = false;
  int scan_cursor_ = 0;
  int scan_direction_ = 1;
  int scan_walked_ = 0;

  // Change-based modes: git diff-tree runs in several processes at once. Each
  // is started once per query and given the next unchecked commits in scan
  // order as it gets through them, a fraction of a second of work ahead, so
  // the nearer commits are always the ones being checked and the search can
  // change course without stopping them. Stepping to a match only walks past
  // commits whose result is known, so a commit further along coming back
  // first never makes it skip a match.
  struct SearchProcess {
    explicit SearchProcess(std::function<void(bool)> busy_callback)
        : search(std::move(busy_callback)) {}
    GitChangeSearch search;
    int run_id = 0;
    bool running = false;
    // Told to end once it has checked its commits.
    bool finishing = false;
    // The commits given to git and not checked yet, in order.
    std::deque<int> queued;
    // Since the front commit got to the front.
    base::ElapsedTimer check_timer;
    // How long checking a commit takes, smoothed, and whether anything has
    // been measured since the process started.
    double seconds_per_commit = 0;
    bool timed = false;
    size_t events_seen = 0;
  };
  std::vector<std::unique_ptr<SearchProcess>> slots_;
  int next_run_id_ = 0;

  int visible_first_ = -1;
  int visible_last_ = -1;
  base::ElapsedTimer visible_timer_;
  bool visible_pending_ = false;

  // kContaining: objects whose labels changed, reused between frames.
  std::vector<std::string> changed_objects_;

  bool is_change_mode() const { return mode_ != CommitSearchMode::kContaining; }

  // Starts searching beyond the visible rows once the query has settled.
  void ApplyQuery();
  // Forgets what is known about every commit and stops checking.
  void ResetResults();
  void SyncStateSize(int n);
  void SetState(int index, State state);

  // kContaining: checks the visible rows that are not known yet.
  void CheckVisibleContaining();
  // kContaining: re-checks commits whose labels changed.
  void ApplyLabelChanges();

  // Checks more commits from the scan cursor on: on the main thread within
  // the time budget for kContaining, by giving the git processes more
  // commits otherwise.
  void CheckMore();
  void CheckMoreContaining();
  // Gives each process the next unchecked commits, up to its share.
  void FillSlots();
  // Returns the next unchecked commit from the scan cursor on, moving past
  // it, or -1 once the walk has covered the list.
  int NextUnchecked();
  // Starts git for the current query. Returns false if git could not be
  // started, which stops the search.
  bool StartSlot(SearchProcess& slot);
  void ProcessEvents();
  void ProcessSlotEvents(SearchProcess& slot);
  bool any_slot_running() const;

  // Makes sure the commit at |index| is checked soon, moving the scan there if
  // it is not about to reach it.
  void EnsureChecked(int index, int direction);
  // Stepping to a match reached the unchecked commit at |index|: stops the
  // processes busy with commits the step doesn't need next, so they take the
  // step's commits right away. Returns true if it stopped any.
  bool ClearPathForStep(int index, int direction);
  // Moves the scan to the visible rows once they settle.
  void PrioritizeVisible();
  void RestartScan(int index, int direction);
  void StopSlot(SearchProcess& slot);
  void StopAllSlots();

  void UpdateNavigation();
  void FinishNavigation(int result);
};

#endif  // GEL_UI_COMMIT_SEARCH_H
