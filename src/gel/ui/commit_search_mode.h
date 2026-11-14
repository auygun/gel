// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_COMMIT_SEARCH_MODE_H
#define GEL_UI_COMMIT_SEARCH_MODE_H

// What the toolbar commit search matches the term against.
enum class CommitSearchMode {
  // Commit metadata: author, committer, message and ref labels.
  kContaining,
  // Space-separated paths or globs the commit changes.
  kTouchingPaths,
  // A string whose number of occurrences the commit changes (git log -S).
  kAddingRemoving,
  // A regex matching a line the commit adds or removes (git log -G).
  kChangingLines,
  kCount
};

inline const char* GetCommitSearchModeName(CommitSearchMode mode) {
  switch (mode) {
    case CommitSearchMode::kContaining:
      return "Containing";
    case CommitSearchMode::kTouchingPaths:
      return "Touching paths";
    case CommitSearchMode::kAddingRemoving:
      return "Adding/removing string";
    case CommitSearchMode::kChangingLines:
      return "Changing lines matching";
    case CommitSearchMode::kCount:
      break;
  }
  return "";
}

// What a search in |mode| finds, for tooltips.
inline const char* GetCommitSearchModeDescription(CommitSearchMode mode) {
  switch (mode) {
    case CommitSearchMode::kContaining:
      return "Commits whose message, author, committer, tags or branches "
             "contain the term";
    case CommitSearchMode::kTouchingPaths:
      return "Commits that change any of the given paths\n"
             "(space-separated, globs allowed)";
    case CommitSearchMode::kAddingRemoving:
      return "Commits that change the number of occurrences of the string\n"
             "(git log -S)";
    case CommitSearchMode::kChangingLines:
      return "Commits that add or remove a line matching the regex\n"
             "(git log -G)";
    case CommitSearchMode::kCount:
      break;
  }
  return "";
}

#endif  // GEL_UI_COMMIT_SEARCH_MODE_H
