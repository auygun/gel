// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_COMMIT_SEARCH_H
#define GEL_UI_COMMIT_SEARCH_H

#include <string>

#include "base/text_search.h"
#include "gel/commands/git_log.h"

// Returns true if any of |commit|'s searchable fields contain |term|: author,
// committer, message, tag names, branch names and the "stash" label. Callers
// scanning many commits should build the term once and pass it in.
bool CommitMatchesSearch(const GitLog::CommitInfo& commit,
                         const base::SearchTerm& term);

// Searches commit fields (see CommitMatchesSearch) on the main thread using
// time-sliced processing. Each frame, processes a chunk of commits within a
// time budget so the UI stays responsive even for very large histories.
class CommitSearch {
 public:
  explicit CommitSearch(GitLog& git_log);
  ~CommitSearch();

  CommitSearch(CommitSearch const&) = delete;
  CommitSearch& operator=(CommitSearch const&) = delete;

  // Starts a new search from |start| in the given |direction|.
  void Search(std::string term,
              int start,
              int direction,
              base::MatchOptions options);

  // Called each frame. Processes a chunk of search work if needed.
  // Returns true if the search is still in progress (caller should keep
  // rendering frames).
  bool Update();

  // Cancels the current search (if any).
  void Cancel();

  // Returns and clears a pending result index, or -1 if none.
  int ConsumeResult();

  // Returns search progress: -1 = idle, [0,1] = in progress.
  float progress() const { return search_progress_; }

 private:
  GitLog& git_log_;

  base::SearchTerm term_;
  int start_ = -1;
  int direction_ = 1;

  float search_progress_ = -1.0f;
  int search_offset_ = 0;  // How many commits checked so far.
  int total_ = 0;          // Total commits to search.
  bool searching_ = false;

  int result_ = -1;  // Pending result for the caller.
};

#endif  // GEL_UI_COMMIT_SEARCH_H
