// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/text_viewer_search.h"

#include <algorithm>

#include "third_party/kaliber/base/timer.h"

TextViewerSearch::TextViewerSearch() = default;

TextViewerSearch::~TextViewerSearch() = default;

bool TextViewerSearch::Update(const std::string& term,
                              base::MatchOptions options,
                              std::span<const std::string> content) {
  base::SearchTerm new_term(term, options);
  int line_count = static_cast<int>(content.size());

  if (new_term != term_) {
    term_ = std::move(new_term);
    matches_.clear();
    current_ = -1;
    settle_frames_ = 0;
    search_progress_ = 0;
    last_count_ = line_count;
    search_complete_ = term_.empty();
  } else if (!term_.empty() && line_count != last_count_) {
    if (last_count_ < 0) {
      // Content changed entirely. Full rebuild.
      matches_.clear();
      current_ = -1;
      search_progress_ = 0;
      search_complete_ = false;
      settle_frames_ = 0;
    } else if (line_count > last_count_) {
      // New lines streamed in. Resume searching from where we left off.
      search_progress_ = std::min(search_progress_, last_count_);
      search_complete_ = false;
      settle_frames_ = 0;
    }
    last_count_ = line_count;
  }

  // Process a chunk of lines within a time budget.
  if (!search_complete_) {
    base::ElapsedTimer timer;
    constexpr double kBudgetSeconds = 0.008;
    int i = search_progress_;

    while (i < line_count) {
      base::ForEachMatch(content[i], term_, buf_, [&](size_t pos) {
        matches_.push_back({i, static_cast<int>(pos)});
      });
      i++;

      // Check time budget every 1024 lines to amortize the clock call.
      if ((i & 1023) == 0 && timer.Elapsed() >= kBudgetSeconds)
        break;
    }

    search_progress_ = i;
    if (i >= line_count) {
      // All known lines searched. Wait a few frames for more lines to stream
      // in before reporting complete, to avoid the "+" indicator blinking.
      constexpr int kSettleFrames = 30;
      settle_frames_++;
      if (settle_frames_ >= kSettleFrames)
        search_complete_ = true;
    }
  }

  return !search_complete_;
}

void TextViewerSearch::Navigate(int direction) {
  if (matches_.empty())
    return;
  int n = static_cast<int>(matches_.size());
  if (current_ < 0)
    current_ = direction > 0 ? 0 : n - 1;
  else
    current_ = ((current_ + direction) % n + n) % n;
  scroll_to_current_ = true;
}

void TextViewerSearch::NavigateToLine(int line) {
  if (matches_.empty())
    return;
  auto it = std::lower_bound(matches_.begin(), matches_.end(), line,
                             [](const Match& m, int l) { return m.line < l; });
  current_ = static_cast<int>(it - matches_.begin());
  if (current_ >= static_cast<int>(matches_.size()))
    current_ = 0;
  scroll_to_current_ = true;
}

void TextViewerSearch::Cancel() {
  current_ = -1;
}

void TextViewerSearch::Reset() {
  current_ = -1;
  last_count_ = -1;
}

bool TextViewerSearch::ConsumeScrollRequest() {
  bool request = scroll_to_current_;
  scroll_to_current_ = false;
  return request;
}
