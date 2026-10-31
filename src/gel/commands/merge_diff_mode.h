// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_MERGE_DIFF_MODE_H
#define GEL_COMMANDS_MERGE_DIFF_MODE_H

// How a merge commit is diffed.
enum class MergeDiffMode {
  // Combined diff against all parents (--cc): only the hunks where the merge
  // differs from every parent, i.e. conflict resolutions.
  kCombined,
  // Normal diff against the first parent: everything the merge brought in.
  kFirstParent,
  kCount,
};

#endif  // GEL_COMMANDS_MERGE_DIFF_MODE_H
