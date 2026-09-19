// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_TEXT_VIEWER_SEARCH_H
#define GEL_UI_MODULES_TEXT_VIEWER_SEARCH_H

#include <span>
#include <string>
#include <vector>

#include "base/text_search.h"

// Searches text content on the main thread using time-sliced processing.
// Each frame, processes a chunk of lines within a time budget so the UI stays
// responsive even for very large content. Uses a reusable string buffer to
// avoid per-line allocations in the hot loop.
class TextViewerSearch {
 public:
  struct Match {
    int line;
    int offset;
  };

  TextViewerSearch();
  ~TextViewerSearch();

  TextViewerSearch(TextViewerSearch const&) = delete;
  TextViewerSearch& operator=(TextViewerSearch const&) = delete;

  // Called each frame. Processes a chunk of search work if needed.
  // Returns true if the search is still in progress (caller should keep
  // rendering frames).
  bool Update(const std::string& term,
              base::MatchOptions options,
              std::span<const std::string> content);

  // Navigates to the next (direction=1) or previous (direction=-1) match.
  void Navigate(int direction);

  // Navigates to the first match at or past |line|.
  void NavigateToLine(int line);

  // Cancels the current search.
  void Cancel();

  // Resets state for new content (keeps the term for re-search).
  void Reset();

  const base::SearchTerm& term() const { return term_; }
  const std::vector<Match>& matches() const { return matches_; }
  int current() const { return current_; }
  bool search_complete() const { return search_complete_; }

  // Returns and clears the pending scroll request.
  bool ConsumeScrollRequest();

 private:
  base::SearchTerm term_;
  std::vector<Match> matches_;
  int current_ = -1;
  int search_progress_ = 0;
  int last_count_ = 0;
  bool search_complete_ = true;
  int settle_frames_ = 0;

  std::string buf_;  // Reusable fold buffer, avoids per-line allocations.
  bool scroll_to_current_ = false;
};

#endif  // GEL_UI_MODULES_TEXT_VIEWER_SEARCH_H
