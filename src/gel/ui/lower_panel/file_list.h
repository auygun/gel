// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_FILE_LIST_H
#define GEL_UI_LOWER_PANEL_FILE_LIST_H

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"

class GitCmdRunner;
class GitDiff;
class GitLog;
class PersistentSettings;
struct FileEntry;

// Renders the file list sidebar showing changed files for the current diff,
// either as a flat list of paths or as a directory tree.
class FileList {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // Returns the index of the selected row in GitLog::GetCommits(),
    // or -1 if no real commit is selected.
    virtual int GetSelectedCommitIndex() const = 0;
    virtual bool IsUnstagedSelected() const = 0;
    virtual bool IsStagedSelected() const = 0;
    virtual void OnShowImageDiff(const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree = false) = 0;
  };

  using ConfirmCallback = std::function<void(const std::string& title,
                                             const std::string& message,
                                             std::function<void()> on_confirm)>;

  FileList(Delegate& delegate,
           GitDiff& git_diff,
           GitLog& git_log,
           GitCmdRunner& runner,
           PersistentSettings& settings,
           ConfirmCallback on_confirm);

  FileList(const FileList&) = delete;
  FileList& operator=(const FileList&) = delete;

  // Renders the file list panel. |width| is the child window width.
  void Update(float width);

  // Returns the currently selected file index, or -1 if none.
  int selected_file() const { return selected_file_; }

  // Returns the file index clicked this frame, or -1 if none. Resets after
  // being read.
  int ConsumeClickedFile();

  // Sets the selected file and optionally scrolls it into view.
  void SetSelectedFile(int index, bool scroll_into_view);

  // Scrolls the list to show the selected file.
  void ScrollToSelected();

  // Expands or collapses all directories of the tree view, including ones
  // that arrive later, in this and other commits.
  void SetAllDirsOpen(bool open);

  void Reset();
  void RevertAll();

  // Returns the parent hashes of |commit| in order, read from the diff. Empty
  // if the diff is not showing |commit| (yet).
  std::vector<std::string> GetCommitParents(const std::string& commit) const;

 private:
  // A directory in the tree view.
  struct TreeDir {
    std::string path;  // Full path, empty for the root.
    int parent = -1;   // -1 for the root.
    // The row showing this directory: its own row, shared with the
    // directories it is folded with, if |visible|. Otherwise the row of the
    // collapsed directory hiding it. Unused for the root.
    int row = -1;
    bool visible = false;
    bool open = true;
    // Children in order of first appearance in the diff, so the tree reads
    // top to bottom in diff order: file indices (>= 0) and directories
    // encoded as ~dir_index (< 0).
    std::vector<int> children;
  };

  // A visible row of the tree view.
  struct TreeRow {
    int item = 0;  // File index (>= 0) or ~dir_index (< 0).
    int depth = 0;
    // Directory rows only: where the label starts in the directory's path. A
    // chain of directories that only contain a single directory is shown as
    // one row labeled with the joined names.
    int label_offset = 0;
  };

  struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const {
      return std::hash<std::string_view>{}(s);
    }
  };

  // Flag marking the selection storage ID of a directory row in the tree
  // view. Directory rows are not multi-select items, but a range selection
  // spanning them adds them. They are removed from the selection right away.
  static constexpr ImGuiID kDirRowIdFlag = 0x80000000u;

  void RenderFileRow(std::span<const FileEntry> files,
                     int index,
                     int row,
                     float text_offset,
                     const char* label,
                     bool is_multi,
                     bool is_unstaged,
                     bool is_staged);
  void RenderFileContextMenu(std::span<const FileEntry> files,
                             int index,
                             bool is_multi,
                             bool is_unstaged,
                             bool is_staged);
  void RenderDirRow(const TreeRow& row, bool selected);

  // Maps a tree row to a selection storage ID: the file index, or the row
  // with kDirRowIdFlag set for a directory row.
  ImGuiID RowToStorageId(int row) const;
  // Applies multi-selection requests from ImGui to |multi_selection_|.
  void ApplySelectionRequests(ImGuiMultiSelectIO* ms_io, bool tree);

  // Adds files appended to the diff since the last call to the tree and
  // reflattens it if needed.
  void SyncTree(std::span<const FileEntry> files);
  // Adds file |index| to the tree. Updates the rows in place when they only
  // change at the end, which is the case when files arrive in git's sorted
  // path order. Otherwise marks the tree dirty for a full reflatten.
  void AddFileToTree(const FileEntry& entry, int index);
  void ClearTree();
  void FlattenTree();
  void FlattenDir(int dir, int depth);
  // Appends the rows of |child| of a directory, a file index or ~dir_index,
  // and its subtree.
  void FlattenChild(int child, int depth);
  void HideSubtree(int dir, int row);
  void HideChild(int child, int row);
  void SetDirOpen(int dir, bool open);

  Delegate& delegate_;
  GitDiff& git_diff_;
  GitLog& git_log_;
  GitCmdRunner& runner_;
  PersistentSettings& settings_;
  ConfirmCallback on_confirm_;

  std::vector<std::string> GetSelectedPaths(bool include_old_paths = false);

  // Renders one menu item per parent in |parents|, labeled with its short
  // hash and subject. Returns the 1-based index of the clicked parent, or 0.
  int ParentMenuItems(const std::vector<std::string>& parents) const;

  int selected_file_ = -1;
  int clicked_file_ = -1;
  int file_count_ = 0;
  bool scroll_file_list_ = false;

  // Keyed by file index in both views.
  ImGuiSelectionBasicStorage multi_selection_;

  // Tree view state. Built lazily and incrementally while in tree view.
  std::vector<TreeDir> tree_dirs_;  // [0] is the root.
  std::unordered_map<std::string, int, StringHash, std::equal_to<>>
      tree_dir_index_;
  std::vector<TreeRow> tree_rows_;
  // Row of each file, or of its collapsed ancestor if the file is hidden.
  std::vector<int> file_row_;
  int tree_file_count_ = 0;
  bool tree_dirty_ = false;
  // Whether directories are open, unless listed in |toggled_dirs_|. Set by
  // expand all / collapse all. Both are kept across commits. Directories
  // start collapsed.
  bool dirs_open_by_default_ = false;
  // Paths of directories the user expanded or collapsed away from the
  // default.
  std::unordered_set<std::string, StringHash, std::equal_to<>> toggled_dirs_;

  void PrepareRevert();
};

#endif  // GEL_UI_LOWER_PANEL_FILE_LIST_H
