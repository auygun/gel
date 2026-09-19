// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MAIN_WINDOW_H
#define GEL_UI_MAIN_WINDOW_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/command_line.h"
#include "gel/commands/git_diff.h"
#include "gel/commands/git_local_status.h"
#include "gel/commands/git_log.h"
#include "gel/git_repo.h"
#include "gel/persistent_settings.h"
#include "gel/ui/commit_search.h"
#include "gel/ui/directory_browser.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/git_console_window.h"
#include "gel/ui/help_modal.h"
#include "gel/ui/image_diff_viewer.h"
#include "gel/ui/lower_panel/blame_lookup.h"
#include "gel/ui/lower_panel/commit_diff.h"
#include "gel/ui/lower_panel/commit_size.h"
#include "gel/ui/popup_modal.h"
#include "gel/ui/settings_modal.h"
#include "gel/ui/style.h"
#include "gel/ui/toolbar.h"
#include "gel/ui/upper_panel/commit_history.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/platform/platform.h"
#include "third_party/kaliber/renderer/renderer.h"

// Owns all UI panels, git workers, and delegate logic for the main application
// window. Gel retains ownership of platform lifecycle, renderer, and ImGui
// backend; MainWindow handles everything inside the frame.
class MainWindow final : public CommitHistory::Delegate,
                         public CommitDiff::Delegate,
                         public Toolbar::Delegate,
                         public DirectoryBrowser::Delegate {
 public:
  MainWindow(eng::Platform& platform,
             PersistentSettings& settings,
             std::function<void(bool)> on_busy_changed,
             std::function<void()> main_thread_busy);
  ~MainWindow();

  struct UpdateResult {
    std::optional<eng::RendererType> pending_renderer;
    std::optional<PendingFont> pending_font;
  };

  // One-time setup: parse command line, start workers.
  void Initialize(const CommandLine& command_line);

  // Per-frame: merge worker data and render UI. Call between NewFrame and Draw.
  UpdateResult Update(float delta_time,
                      eng::Renderer& renderer,
                      ImTextureID icon_texture,
                      int active_bg_tasks);

  // Queues a modal popup to be shown on the next frame.
  void ShowPopup(std::string_view title,
                 std::string message,
                 bool exit_on_close = false);

  // Sets the initial pending style so it is applied on the first frame.
  void SetPendingStyle(Style style, Layout layout, float font_scale);

  // CSD (client-side decoration) state.
  void SetUsingCSD(bool csd, bool show_minimize, bool show_maximize);

  // CSD interactive state (move/resize in progress).
  void SetCSDInteractive(bool interactive) { csd_interactive_ = interactive; }

  // Clears the interactive flag and returns whether it was set.
  bool ConsumeCSDInteractive();

  // Platform focus events forwarded from Gel.
  void OnLostFocus();
  void OnGainedFocus();

  bool using_csd() const { return using_csd_; }
  bool IsWindowFocused() const final { return window_focused_; }
  bool IsSettingsModalOpen() const;
  bool IsDirectoryBrowserOpen() const;

 private:
  eng::Platform& platform_;
  PersistentSettings& settings_;
  eng::Renderer* renderer_ = nullptr;
  std::function<void(bool)> on_busy_changed_;
  std::function<void()> main_thread_busy_;

  // Background workers that run git commands and parse output asynchronously.
  GitRepo git_repo_;
  GitLog git_log_;
  GitDiff git_diff_;
  GitLocalStatus git_status_;
  GitCmdRunner runner_;

  // UI panel classes that own rendering and state for their respective panels.
  CommitHistory commit_history_;
  CommitDiff commit_diff_;
  CommitSize commit_size_;
  Toolbar toolbar_;
  SettingsModal settings_modal_;
  HelpModal help_modal_;
  GitConsoleWindow console_window_;
  std::vector<std::unique_ptr<ImageDiffViewer>> image_diff_viewers_;
  int next_image_diff_id_ = 0;

  BlameLookup blame_lookup_;
  CommitSearch commit_search_;

  PopupModal popup_modal_;
  DirectoryBrowser directory_browser_;

  std::optional<std::tuple<Style, Layout, float>> pending_style_;

  bool using_csd_ = false;
  bool csd_interactive_ = false;
  bool window_focused_ = true;
  bool show_minimize_button_ = true;
  bool show_maximize_button_ = true;

  std::vector<eng::Platform::FontInfo> fonts_;
  bool commit_search_busy_ = false;
  bool diff_stale_ = false;
  bool browsing_specific_ref_ = false;
  std::string window_title_;
  std::string program_path_;

  void Refresh();
  // Re-runs the diff for the current selection. With |preserve_scroll| the
  // diff panel keeps showing the same content across the reload, for re-runs
  // that only change how the same diff is rendered (e.g. lines of context).
  void RerunDiff(bool preserve_scroll = false);
  void UpdateWindowTitle(const std::vector<std::string>& extra_args);
  void MergeWorkerData();
  void HandleCSDResize();
  void ApplyPendingStyle(eng::Renderer& renderer);
  void HandleKeyboardShortcuts();

  // CommitHistory::Delegate implementation
  void OnCommitSelected(const std::string& commit,
                        bool reselected,
                        bool from_history) final;
  void OnRevertAllRequested() final;

  // CommitDiff::Delegate implementation
  int GetSelectedCommitIndex() const final;
  void OnShowImageDiff(const std::string& old_ref,
                       const std::string& new_ref,
                       const std::string& path,
                       bool new_from_worktree) final;
  void SetPrimarySelection(const std::string& text) final;
  void OnShowLineOrigin(const std::string& file,
                        int line_number,
                        bool is_old_side) final;
  void OnRunGitGuiBlame(const std::string& file,
                        int line_number,
                        bool is_old_side) final;
  bool IsUnstagedSelected() const final;
  bool IsStagedSelected() const final;
  void OnBlameSearchCancel() final;
  void UpdateLinesOfContext(int ctx) final;
  void OnClearDiffPathFilterRequested() final;
  void OnDiffRefreshRequested() final;
  bool HasDiffPathFilter() const final;
  void OnNavigateToCommit(const std::string& commit) final;

  // Toolbar::Delegate implementation
  void SelectCommitByHash(std::string_view query) final;
  int GetTotalRowCount() const final;
  int GetSelectedRowIndex() const final;
  void OnRefreshRequested() final;
  void OnSettingsRequested() final;
  void OnSearchRequested(std::string term, int direction) final;
  void OnHelpRequested() final;
  bool HasLogPathFilter() const final;
  void OnClearPathFilterRequested() final;
  void OnToggleLowerPanel() final;
  bool IsSizePanelActive() const final;
  bool IsUsingCSD() const final;
  bool IsMaximized() const final;
  bool ShowMinimizeButton() const final;
  bool ShowMaximizeButton() const final;
  void OnMinimizeRequested() final;
  void OnMaximizeToggleRequested() final;
  void OnCloseRequested() final;
  void OnTitleBarDragRequested() final;
  void OnWindowMenuRequested(int x, int y) final;
  const std::string& GetWindowTitle() const final;
  void OnBranchSwitchRequested(const std::string& branch) final;
  void OnConsoleWindowRequested() final;

  // DirectoryBrowser::Delegate implementation
  void OnDirectorySelected(const std::filesystem::path& path) final;
  void OnCreateRepository(const std::filesystem::path& path) final;
  void OnBrowserDismissed() final;
};

#endif  // GEL_UI_MAIN_WINDOW_H
