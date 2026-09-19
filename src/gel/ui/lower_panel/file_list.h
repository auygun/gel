// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_FILE_LIST_H
#define GEL_UI_LOWER_PANEL_FILE_LIST_H

#include <functional>
#include <string>
#include <vector>

#include "third_party/imgui/imgui/imgui.h"

class GitCmdRunner;
class GitDiff;
class GitLog;
class PersistentSettings;

// Renders the file list sidebar showing changed files for the current diff.
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

  void Reset();
  void RevertAll();

 private:
  Delegate& delegate_;
  GitDiff& git_diff_;
  GitLog& git_log_;
  GitCmdRunner& runner_;
  PersistentSettings& settings_;
  ConfirmCallback on_confirm_;

  std::vector<std::string> GetSelectedPaths(bool include_old_paths = false);

  int selected_file_ = -1;
  int clicked_file_ = -1;
  int file_count_ = 0;
  bool scroll_file_list_ = false;

  ImGuiSelectionBasicStorage multi_selection_;

  void PrepareRevert();
};

#endif  // GEL_UI_LOWER_PANEL_FILE_LIST_H
