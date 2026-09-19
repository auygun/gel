// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_DIRECTORY_BROWSER_H
#define GEL_UI_DIRECTORY_BROWSER_H

#include <filesystem>
#include <string>
#include <vector>

class DirectoryBrowser {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnDirectorySelected(const std::filesystem::path& path) = 0;
    virtual void OnCreateRepository(const std::filesystem::path& path) = 0;
    virtual void OnBrowserDismissed() = 0;
  };

  explicit DirectoryBrowser(Delegate& delegate);

  void Open();
  void Close();
  void Update(bool window_focused);
  bool IsOpen() const { return open_; }

 private:
  enum class SortOrder { kGitFirst, kName };

  struct DirNode {
    std::string name;
    std::filesystem::path full_path;
    bool is_git_root = false;
    bool is_inside_repo = false;
    bool is_open = false;
    bool children_loaded = false;
    std::vector<DirNode> children;

    bool IsGitRepo() const { return is_git_root || is_inside_repo; }
  };

  struct FlatRow {
    DirNode* node = nullptr;
    int depth = 0;
  };

  Delegate& delegate_;
  DirNode root_;
  DirNode* selected_ = nullptr;
  std::vector<FlatRow> flat_tree_;
  bool open_ = false;
  bool show_ = false;
  int scroll_to_row_ = -1;

  char path_input_[1024] = {};
  SortOrder sort_order_ = SortOrder::kGitFirst;
  bool sort_tooltip_suppressed_ = false;

  bool focus_tree_ = false;
  bool focus_path_input_ = false;
  bool focus_after_tree_ = false;
  DirNode* renaming_ = nullptr;
  char rename_input_[256] = {};
  bool rename_focus_ = false;

  void LoadChildren(DirNode& node);
  void SortChildren(DirNode& node);
  void ResortAll(DirNode& node);
  void NavigateToPath(const std::filesystem::path& path);
  void ExpandToPath(const std::filesystem::path& path);
  void CreateDirectory();
  void DeleteDirectory();
  void CommitRename();
  void CancelRename();
  void FlattenTree();
  void FlattenNode(DirNode& node, int depth);
  void RenderTree();
  void RenderSortButtons();
};

#endif  // GEL_UI_DIRECTORY_BROWSER_H
