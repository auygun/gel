// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/commit_search.h"

#include "third_party/kaliber/base/timer.h"

bool CommitMatchesSearch(const GitLog::CommitInfo& commit,
                         const base::SearchTerm& term) {
  if (base::Contains(commit.author, term) ||
      base::Contains(commit.committer, term)) {
    return true;
  }
  for (const auto& line : commit.message) {
    if (base::Contains(line, term))
      return true;
  }
  for (const auto& tag : commit.tags) {
    if (base::Contains(tag, term))
      return true;
  }
  for (const auto& branch : commit.branches) {
    if (base::Contains(branch.name, term))
      return true;
  }
  return commit.is_stash && base::Contains("stash", term);
}

CommitSearch::CommitSearch(GitLog& git_log) : git_log_(git_log) {}

CommitSearch::~CommitSearch() = default;

void CommitSearch::Search(std::string term,
                          int start,
                          int direction,
                          base::MatchOptions options) {
  term_ = base::SearchTerm(term, options);
  start_ = start;
  direction_ = direction;
  search_offset_ = 0;
  total_ = static_cast<int>(git_log_.GetCommits().size());
  searching_ = !term_.empty() && total_ > 0;
  search_progress_ = searching_ ? 0.0f : -1.0f;
  result_ = -1;
}

bool CommitSearch::Update() {
  if (!searching_)
    return false;

  auto commits = git_log_.GetCommits();
  int n = static_cast<int>(commits.size());
  if (n == 0) {
    searching_ = false;
    search_progress_ = -1.0f;
    return false;
  }

  // Start from next/previous commit, wrapping around.
  int begin = start_ < 0 ? 0 : ((start_ + direction_) % n + n) % n;

  base::ElapsedTimer timer;
  constexpr double kBudgetSeconds = 0.008;

  while (search_offset_ < n) {
    // Wrap the index into [0, n) regardless of search direction.
    // The double-mod handles negative values from reverse searches.
    int i = ((begin + direction_ * search_offset_) % n + n) % n;
    if (CommitMatchesSearch(commits[i], term_)) {
      result_ = i;
      searching_ = false;
      search_progress_ = -1.0f;
      return false;
    }
    search_offset_++;

    // Check time budget every 1024 commits to amortize the clock call.
    if ((search_offset_ & 1023) == 0 && timer.Elapsed() >= kBudgetSeconds)
      break;
  }

  if (search_offset_ >= n) {
    // Searched everything, no match found.
    searching_ = false;
    search_progress_ = -1.0f;
  } else {
    search_progress_ = static_cast<float>(search_offset_) / n;
  }

  return searching_;
}

void CommitSearch::Cancel() {
  searching_ = false;
  search_progress_ = -1.0f;
}

int CommitSearch::ConsumeResult() {
  int r = result_;
  result_ = -1;
  return r;
}
