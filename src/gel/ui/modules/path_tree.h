// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_PATH_TREE_H
#define GEL_UI_MODULES_PATH_TREE_H

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// A directory tree over a list of paths owned by the caller, flattened into
// the rows that are visible with the current expanded state, for rendering
// with a clipper. Items are indices into the caller's list and are added in
// index order, so the tree can grow while the list streams in. Rows are
// updated in place when paths arrive in git's sorted path order; otherwise the
// tree is reflattened on the next FlattenIfDirty().
//
// A chain of directories that only contain a single directory is shown as one
// row labeled with the joined names. Directories keep their expanded state
// across Clear(), by path.
class PathTree {
 public:
  // A visible row.
  struct Row {
    int item = 0;  // Item index (>= 0) or ~dir_index (< 0).
    int depth = 0;
    // Directory rows only: where the label starts in the directory's path.
    int label_offset = 0;
  };

  explicit PathTree(bool dirs_open_by_default = true);

  PathTree(const PathTree&) = delete;
  PathTree& operator=(const PathTree&) = delete;

  // Removes all items. The expanded state of directories is kept.
  void Clear();

  // Adds the next item, with index item_count(), at |path|. An item with an
  // empty path is shown at the top level.
  void Add(std::string_view path);

  int item_count() const { return static_cast<int>(item_row_.size()); }

  // Reflattens the tree if items or expanded directories changed the rows in
  // a way that couldn't be applied in place. Call before reading rows(), and
  // after rendering to apply directories toggled by RenderDirRow().
  void FlattenIfDirty();

  std::span<const Row> rows() const { return rows_; }

  // Returns the row showing |item|: its own row, or the row of the collapsed
  // directory hiding it. -1 if |item| is out of range.
  int RowOfItem(int item) const;

  // Renders a directory row: an expand arrow and the directory's name, below
  // the names of its parent row. Clicking it expands or collapses the
  // directory, applied by the next FlattenIfDirty().
  void RenderDirRow(const Row& row, bool selected);

  // Expands or collapses all directories, including ones added later.
  void SetAllDirsOpen(bool open);

  // Where the text of an item row at |depth| starts, so that it lines up
  // with the names of directory rows at the same depth.
  static float ItemTextOffset(int depth);

  // Returns the last component of |path|.
  static const char* ItemName(const std::string& path);

 private:
  // A directory.
  struct Dir {
    std::string path;  // Full path, empty for the root.
    int parent = -1;   // -1 for the root.
    // The row showing this directory: its own row, shared with the
    // directories it is folded with, if |visible|. Otherwise the row of the
    // collapsed directory hiding it. Unused for the root.
    int row = -1;
    bool visible = false;
    bool open = true;
    // Children in order of first appearance, so the tree reads top to bottom
    // in item order: item indices (>= 0) and directories encoded as
    // ~dir_index (< 0).
    std::vector<int> children;
  };

  struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const {
      return std::hash<std::string_view>{}(s);
    }
  };

  void Flatten();
  void FlattenDir(int dir, int depth);
  // Appends the rows of |child| of a directory, an item index or ~dir_index,
  // and its subtree.
  void FlattenChild(int child, int depth);
  void HideSubtree(int dir, int row);
  void HideChild(int child, int row);
  void SetDirOpen(int dir, bool open);

  std::vector<Dir> dirs_;  // [0] is the root.
  std::unordered_map<std::string, int, StringHash, std::equal_to<>> dir_index_;
  std::vector<Row> rows_;
  // Row of each item, or of its collapsed ancestor if the item is hidden.
  std::vector<int> item_row_;
  bool dirty_ = false;
  // Whether directories are open, unless listed in |toggled_dirs_|. Set by
  // SetAllDirsOpen().
  bool dirs_open_by_default_;
  // Paths of directories the user expanded or collapsed away from the
  // default.
  std::unordered_set<std::string, StringHash, std::equal_to<>> toggled_dirs_;
};

#endif  // GEL_UI_MODULES_PATH_TREE_H
