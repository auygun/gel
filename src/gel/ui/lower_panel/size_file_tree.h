// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_SIZE_FILE_TREE_H
#define GEL_UI_LOWER_PANEL_SIZE_FILE_TREE_H

#include <cstdint>
#include <string>
#include <vector>

// A file changed by a commit with its new blob size.
struct ChangedFileEntry {
  std::string path;
  int64_t size = 0;
};

class PersistentSettings;

// File tree panel for the size view. Builds an in-memory tree from a flat
// file list, renders it with expand/collapse, and notifies a delegate when
// the user selects a directory.
class SizeFileTree {
 public:
  // A node in the in-memory tree built from the flat file list.
  struct TreeNode {
    std::string name;
    std::string full_path;  // Full path from root (empty for root node).
    int64_t size = 0;       // Own size for files, sum of children for dirs.
    bool is_directory = false;
    bool open = false;
    std::vector<TreeNode> children;
  };

  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnDirectorySelected(TreeNode* node) = 0;
  };

  SizeFileTree(Delegate& delegate, PersistentSettings& settings);

  // Renders the file tree (call within an ImGui child window).
  void Update();

  // Receives file data, builds/sorts/flattens the tree, selects root.
  void SetFiles(std::vector<ChangedFileEntry> files);

  // Resets all tree state.
  void Clear();

  // Sets the selected directory and notifies the delegate.
  void SelectDirectory(TreeNode* node);

  // Opens ancestors of |node|, reflattens, and scrolls to it.
  void ScrollToDirectory(TreeNode* node);

  TreeNode* GetSelectedDirectory() { return selected_dir_; }
  const TreeNode* GetSelectedDirectory() const { return selected_dir_; }
  TreeNode* GetRoot() { return &root_; }
  TreeNode* FindNode(const std::string& path);

  bool HasData() const { return !files_.empty(); }

 private:
  // A visible row in the flattened tree, used by the clipper.
  struct FlatRow {
    TreeNode* node = nullptr;
    int depth = 0;
  };

  void BuildTree();
  void SortTree();
  void FlattenTree();
  void FlattenNode(TreeNode& node, int depth);
  void OpenAncestors(TreeNode* target);

  void RenderSortButtons(float btn_size);
  void RenderFileTree();

  Delegate& delegate_;
  PersistentSettings& settings_;

  std::vector<ChangedFileEntry> files_;
  TreeNode root_;
  TreeNode* selected_dir_ = nullptr;
  std::vector<FlatRow> flat_tree_;

  bool scroll_to_selected_ = false;
  int scroll_to_row_ = -1;
  bool sort_tooltip_suppressed_ = false;
};

#endif  // GEL_UI_LOWER_PANEL_SIZE_FILE_TREE_H
