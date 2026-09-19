// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_TOOLBAR_H
#define GEL_UI_TOOLBAR_H

#include <filesystem>
#include <string>
#include <vector>

#include "gel/ui/modules/search_bar.h"
#include "third_party/imgui/imgui/imgui.h"

class GitCmdRunner;
class GitRepo;
class PersistentSettings;

// Toolbar rendered at the top of the main window.
class Toolbar {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void SelectCommitByHash(std::string_view query) = 0;
    virtual int GetTotalRowCount() const = 0;
    virtual int GetSelectedRowIndex() const = 0;
    virtual void OnRefreshRequested() = 0;
    virtual void OnSettingsRequested() = 0;
    virtual void OnSearchRequested(std::string term, int direction) = 0;
    virtual void OnHelpRequested() = 0;
    virtual bool HasDiffPathFilter() const = 0;
    virtual bool HasLogPathFilter() const = 0;
    virtual void OnClearPathFilterRequested() = 0;
    virtual void OnToggleLowerPanel() = 0;
    virtual bool IsSizePanelActive() const = 0;
    virtual bool IsUsingCSD() const = 0;
    virtual bool IsMaximized() const = 0;
    virtual bool ShowMinimizeButton() const = 0;
    virtual bool ShowMaximizeButton() const = 0;
    virtual void OnMinimizeRequested() = 0;
    virtual void OnMaximizeToggleRequested() = 0;
    virtual void OnCloseRequested() = 0;
    virtual void OnTitleBarDragRequested() = 0;
    virtual void OnWindowMenuRequested(int x, int y) = 0;
    virtual const std::string& GetWindowTitle() const = 0;
    virtual bool IsWindowFocused() const = 0;
    virtual void OnBranchSwitchRequested(const std::string& branch) = 0;
    virtual void OnConsoleWindowRequested() = 0;
  };

  Toolbar(Delegate& delegate,
          GitCmdRunner& runner,
          GitRepo& git_repo,
          PersistentSettings& settings);

  void Update(float search_progress);

  const std::string& search_term() const { return search_term_; }

  // Write a commit hash into the search input field.
  void SetCommitInput(const std::string& commit);

  // Re-reads HEAD and in-progress task state from the .git directory.
  void UpdateHeadStatus();

 private:
  Delegate& delegate_;
  GitCmdRunner& runner_;
  GitRepo& git_repo_;
  PersistentSettings& settings_;

  static constexpr int kCommitInputSize = 41;
  char commit_input_[kCommitInputSize] = {};

  SearchBar search_bar_;

  std::string pending_commit_input_;
  bool commit_input_changed_ = false;
  bool focus_input_ = false;
  int active_field_ = -1;  // 0=commit, 1=search, -1=none
  bool tooltip_suppressed_ = false;

  // CSD button state for foreground draw list rendering and raw input.
  enum class CSDButton { kNone, kMinimize, kMaximize, kClose };
  CSDButton csd_pressed_ = CSDButton::kNone;
  ImVec2 csd_app_icon_min_{};
  ImVec2 csd_app_icon_max_{};

  std::string search_term_;
  std::string head_label_;
  bool head_detached_ = false;
  std::vector<std::string> local_branches_;
  char branch_rename_input_[128] = {};
  char branch_create_input_[128] = {};
  std::filesystem::path common_git_dir_;
};

#endif  // GEL_UI_TOOLBAR_H
