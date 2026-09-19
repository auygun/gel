// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/main_window.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui_internal.h"

using namespace base;
using namespace eng;

MainWindow::MainWindow(Platform& platform,
                       PersistentSettings& settings,
                       std::function<void(bool)> on_busy_changed,
                       std::function<void()> main_thread_busy)
    : platform_(platform),
      settings_(settings),
      on_busy_changed_(std::move(on_busy_changed)),
      main_thread_busy_(std::move(main_thread_busy)),
      git_log_([this](bool b) { on_busy_changed_(b); }),
      git_diff_([this](bool b) { on_busy_changed_(b); }),
      git_status_([this](bool b) { on_busy_changed_(b); }),
      runner_(
          git_repo_,
          [this](bool b) { on_busy_changed_(b); },
          [this](bool success, bool is_cancel, bool local_only) {
            if (!success || is_cancel) {
              toolbar_.UpdateHeadStatus();
              if (!browsing_specific_ref_)
                git_status_.Run();
            } else if (local_only) {
              if (!browsing_specific_ref_)
                git_status_.Run();
              RerunDiff();
            } else {
              Refresh();
            }
          },
          [this](const std::string& error,
                 const std::string& cmd_name,
                 bool show_popup) -> bool {
            std::string tag_name, tag_commit;
            if (cmd_name == "tag" &&
                commit_history_.ConsumePendingTagMove(tag_name, tag_commit)) {
              std::string msg = "Move tag '" + tag_name + "'?";
              popup_modal_.ShowConfirmation(
                  "Tag already exists", std::move(msg),
                  [this, n = std::move(tag_name), c = std::move(tag_commit)]() {
                    runner_.Run({"tag", "-d", n});
                    runner_.RunNext({"tag", n, c});
                  });
              return true;
            }
            if (show_popup)
              ShowPopup(cmd_name + " failed",
                        error.empty() ? "Command failed." : error);
            return false;
          }),
      commit_history_(
          *this,
          git_log_,
          runner_,
          git_repo_,
          settings_,
          [this](const std::string& b) { OnBranchSwitchRequested(b); }),
      commit_diff_(
          *this,
          git_diff_,
          git_log_,
          runner_,
          settings_,
          [this](const std::string& title,
                 const std::string& message,
                 std::function<void()> on_confirm) {
            popup_modal_.ShowConfirmation(title, message,
                                          std::move(on_confirm));
          },
          main_thread_busy_),
      commit_size_(settings_, [this](bool b) { on_busy_changed_(b); }),
      toolbar_(*this, runner_, git_repo_, settings_),
      console_window_(runner_, settings_),
      blame_lookup_(
          commit_history_,
          commit_diff_,
          git_diff_,
          git_log_,
          git_repo_,
          [this](bool b) { on_busy_changed_(b); },
          [this](const std::string& title, std::string message) {
            ShowPopup(title, std::move(message));
          }),
      commit_search_(git_log_),
      popup_modal_([this]() { platform_.Exit(); }),
      directory_browser_(*this) {}

MainWindow::~MainWindow() = default;

bool MainWindow::ConsumeCSDInteractive() {
  bool was = csd_interactive_;
  csd_interactive_ = false;
  return was;
}

bool MainWindow::IsSettingsModalOpen() const {
  return settings_modal_.IsOpen();
}

bool MainWindow::IsDirectoryBrowserOpen() const {
  return directory_browser_.IsOpen();
}

void MainWindow::ShowPopup(std::string_view title,
                           std::string message,
                           bool exit_on_close) {
  popup_modal_.ShowMessage(title, std::move(message), exit_on_close);
}

void MainWindow::SetPendingStyle(Style style, Layout layout, float font_scale) {
  pending_style_ = std::make_tuple(style, layout, font_scale);
}

void MainWindow::SetUsingCSD(bool csd, bool show_minimize, bool show_maximize) {
  using_csd_ = csd;
  show_minimize_button_ = show_minimize;
  show_maximize_button_ = show_maximize;
}

void MainWindow::OnLostFocus() {
  // When CSD move/resize hands control to the window manager, the WM may
  // report a transient focus loss. Suppress it so the title bar text doesn't
  // flash to the disabled color during the drag.
  if (ConsumeCSDInteractive())
    return;
  window_focused_ = false;
}

void MainWindow::OnGainedFocus() {
  csd_interactive_ = false;
  window_focused_ = true;
}

void MainWindow::Initialize(const CommandLine& command_line) {
  program_path_ = command_line.program();
  SetPendingStyle(settings_.style, settings_.layout, settings_.font_scale);

  // Skip gel-specific switches and any git log switches that would change the
  // output format and break GitLog's field parsing.
  auto extra_args = command_line.BuildArgList(
      {'p', 'z'},
      {"pretty", "format", "oneline", "color", "no-color", "raw", "patch",
       "no-patch", "stat", "shortstat", "numstat", "name-only", "name-status",
       "diff-merges", "graph", "abbrev-commit", "no-decorate"});
  git_log_.SetExtraArgs(extra_args);

  // Forward args that git-diff-tree also accepts.
  std::vector<std::string> diff_args;
  for (auto& arg : extra_args) {
    if (arg.starts_with("--date="))
      diff_args.push_back(arg);
  }
  if (!diff_args.empty())
    git_diff_.SetExtraArgs(std::move(diff_args));

  auto& path_filter = command_line.GetPathFilter();
  if (!path_filter.empty()) {
    git_log_.SetPathFilter(path_filter);
    git_diff_.SetPathFilter(path_filter);
    git_status_.SetPathFilter(path_filter);
  }

  UpdateWindowTitle(extra_args);

  // When a branch/commit/tag is passed as a positional argument, we are
  // browsing a specific ref rather than HEAD. Staged/Unstaged rows relate
  // to the current working tree (HEAD) and should not be shown.
  browsing_specific_ref_ = !command_line.GetArgs().empty();

  git_diff_.SetLinesOfContext(settings_.context_lines);
  git_diff_.SetGitRepo(git_repo_);
  git_repo_.Init([this](bool found) {
    if (found) {
      Refresh();
    } else {
      directory_browser_.Open();
    }
  });
}

void MainWindow::Refresh() {
  commit_search_.Cancel();
  commit_history_.ClearSelectionHistory();
  git_log_.Run();
  if (!browsing_specific_ref_)
    git_status_.Run();
  // For real commits the diff self-heal in MergeWorkerData() will detect the
  // selection/diff mismatch and re-trigger automatically. Synthetic rows
  // (unstaged/staged) keep the same identity across refreshes, so the
  // self-heal sees no mismatch — force a rerun here.
  if (commit_history_.IsUnstagedSelected() ||
      commit_history_.IsStagedSelected())
    RerunDiff();
  runner_.UpdateTaskState();
  toolbar_.UpdateHeadStatus();
}

void MainWindow::RerunDiff(bool preserve_scroll) {
  if (settings_.show_size_panel) {
    diff_stale_ = true;
    return;
  }
  if (commit_history_.GetSelectedRowIndex() == -1)
    return;
  if (preserve_scroll)
    commit_diff_.CaptureScrollAnchor();
  git_diff_.SetExtMappings(settings_.ext_mappings);
  if (commit_history_.IsUnstagedSelected())
    git_diff_.RunUnstaged();
  else if (commit_history_.IsStagedSelected())
    git_diff_.RunStaged();
  else
    git_diff_.RunTree({commit_history_.GetSelectedCommit()});
}

void MainWindow::UpdateWindowTitle(const std::vector<std::string>& extra_args) {
  std::string revision_range;
  for (auto& arg : extra_args) {
    if (!arg.starts_with('-')) {
      if (!revision_range.empty())
        revision_range += ' ';
      revision_range += arg;
    }
  }
  window_title_ = std::filesystem::current_path().filename().string();
  if (!revision_range.empty())
    window_title_ += ": " + revision_range;
  window_title_ += " - gel";
  platform_.SetWindowTitle(window_title_);
}

void MainWindow::MergeWorkerData() {
  // Merge any new data from the background git workers into the main-thread
  // buffers.
  git_log_.Update();
  if (git_log_.DidClear()) {
    commit_history_.ResetGraph();
    commit_history_.ScrollToSelected();
    commit_history_.SetInfoMessage({});
  }

  {
    std::string error;
    if (git_log_.GetError(error))
      ShowPopup("Error", error.empty() ? "git log failed." : error);
  }

  {
    std::string info;
    if (git_log_.GetInfoMessage(info))
      commit_history_.SetInfoMessage(std::move(info));
  }

  git_diff_.Update();
  if (git_diff_.DidClear()) {
    commit_diff_.Reset();
    blame_lookup_.OnDiffCleared();
  }

  // Self-heal the diff panel on every frame by comparing GitDiff's last run
  // state against the current selection.
  if (git_diff_.GetLastRunCommitHash() != commit_history_.GetSelectedCommit() ||
      git_diff_.IsLastRunUnstaged() != commit_history_.IsUnstagedSelected() ||
      git_diff_.IsLastRunStaged() != commit_history_.IsStagedSelected()) {
    RerunDiff();
  }

  blame_lookup_.PollBlame();
  blame_lookup_.ScanDiffContent();

  // Process local-changes check result from the background thread.
  if (git_status_.Update()) {
    commit_history_.HandleLocalStatusUpdate(git_status_.has_unstaged(),
                                            git_status_.has_staged());
  }

  runner_.Update();

  // Select the first real commit once it arrives.
  commit_history_.AutoSelectFirst();
}

MainWindow::UpdateResult MainWindow::Update(float delta_time,
                                            Renderer& renderer,
                                            ImTextureID icon_texture,
                                            int active_bg_tasks) {
  renderer_ = &renderer;
  MergeWorkerData();
  HandleCSDResize();
  ApplyPendingStyle(renderer);

  if (!popup_modal_.IsVisible())
    HandleKeyboardShortcuts();

  // Full-window ImGui frame with no decoration or background.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  bool gel_open = ImGui::Begin(
      "Gel", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  bool hide_cursor = !window_focused_;
  if (hide_cursor) {
    ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_InputTextCursor);
    c.w = 0.0f;
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, c);
  }

  UpdateResult result;

  if (gel_open) {
    {
      bool still_searching = commit_search_.Update();
      int search_result = commit_search_.ConsumeResult();
      if (search_result >= 0) {
        if (commit_search_busy_) {
          commit_search_busy_ = false;
          on_busy_changed_(false);
        }
        int row = search_result + commit_history_.SyntheticRows();
        commit_history_.SelectCommit(row);
        commit_history_.ScrollToSelected();
      } else if (commit_search_busy_ && !still_searching) {
        commit_search_busy_ = false;
        on_busy_changed_(false);
      }
    }
    toolbar_.Update(commit_search_.progress());
    commit_history_.Update(toolbar_.search_term());
    if (settings_.show_size_panel)
      commit_size_.Update();
    else
      commit_diff_.Update(toolbar_.search_term());

    popup_modal_.Render(window_focused_);
    directory_browser_.Update(window_focused_);

    auto settings_result =
        settings_modal_.Update(settings_, icon_texture, window_focused_);
    if (settings_result.pending_style)
      pending_style_ = settings_result.pending_style;
    if (settings_result.pending_renderer)
      result.pending_renderer = settings_result.pending_renderer;
    if (settings_result.pending_font)
      result.pending_font = settings_result.pending_font;

    help_modal_.Update(window_focused_);
    console_window_.Update(window_focused_);

    std::erase_if(image_diff_viewers_,
                  [](const auto& v) { return !v->IsOpen(); });
    for (auto& viewer : image_diff_viewers_)
      viewer->Update(renderer, window_focused_);
  }
  if (hide_cursor)
    ImGui::PopStyleColor();
  ImGui::End();

  if (active_bg_tasks > 0)
    DrawMouseSpinner();

#if 0
  ImGui::ShowDemoWindow();
#endif

  return result;
}

void MainWindow::HandleCSDResize() {
#if !defined(OS_APPLE) && !defined(OS_WIN)
  if (using_csd_ && !platform_.IsMaximized()) {
    int win_w = platform_.GetWindowWidth();
    int win_h = platform_.GetWindowHeight();
    int mx = platform_.GetMouseX();
    int my = platform_.GetMouseY();

    constexpr int kBorderSide = 5;
    constexpr int kBorderTop = 3;
    constexpr int kBorderBottom = 5;
    constexpr int kCorner = 10;

    constexpr int kSizeTopLeft = 0;
    constexpr int kSizeTop = 1;
    constexpr int kSizeTopRight = 2;
    constexpr int kSizeRight = 3;
    constexpr int kSizeBottomRight = 4;
    constexpr int kSizeBottom = 5;
    constexpr int kSizeBottomLeft = 6;
    constexpr int kSizeLeft = 7;

    bool at_left = mx < kBorderSide;
    bool at_right = mx >= win_w - kBorderSide;
    bool at_top = my < kBorderTop;
    bool at_bottom = my >= win_h - kBorderBottom;
    bool at_corner_top = my < kCorner;
    bool at_corner_bottom = my >= win_h - kCorner;
    bool at_corner_left = mx < kCorner;
    bool at_corner_right = mx >= win_w - kCorner;

    int direction = -1;
    int cursor = -1;

    if (at_top && at_corner_left) {
      direction = kSizeTopLeft;
      cursor = 6;
    } else if (at_top && at_corner_right) {
      direction = kSizeTopRight;
      cursor = 5;
    } else if (at_bottom && at_corner_left) {
      direction = kSizeBottomLeft;
      cursor = 5;
    } else if (at_bottom && at_corner_right) {
      direction = kSizeBottomRight;
      cursor = 6;
    } else if (at_left && at_corner_top) {
      direction = kSizeTopLeft;
      cursor = 6;
    } else if (at_left && at_corner_bottom) {
      direction = kSizeBottomLeft;
      cursor = 5;
    } else if (at_right && at_corner_top) {
      direction = kSizeTopRight;
      cursor = 5;
    } else if (at_right && at_corner_bottom) {
      direction = kSizeBottomRight;
      cursor = 6;
    } else if (at_top) {
      direction = kSizeTop;
      cursor = 3;
    } else if (at_bottom) {
      direction = kSizeBottom;
      cursor = 3;
    } else if (at_left) {
      direction = kSizeLeft;
      cursor = 4;
    } else if (at_right) {
      direction = kSizeRight;
      cursor = 4;
    }

    if (direction >= 0 && platform_.IsCursorInside()) {
      ImGui::SetMouseCursor(static_cast<ImGuiMouseCursor>(cursor));
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        csd_interactive_ = true;
        platform_.BeginInteractiveResize(direction);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      }
    }
  }
#endif
}

void MainWindow::ApplyPendingStyle(Renderer& renderer) {
  if (!pending_style_)
    return;
  auto [style, layout, scale] = *pending_style_;
  ImGui::GetStyle() = ImGuiStyle();
  ApplyStyle(style, layout, platform_.IsDarkMode());
  auto& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
  renderer.SetClearColor({bg.x, bg.y, bg.z, bg.w});
  float dpi_scale = platform_.GetDeviceScaleFactor();
  ImGui::GetStyle().FontScaleMain = scale;
  ImGui::GetStyle().FontScaleDpi = dpi_scale;
  ImGui::GetStyle().ScaleAllSizes(dpi_scale);
  pending_style_.reset();
}

void MainWindow::HandleKeyboardShortcuts() {
  if (ImGui::GetIO().KeyCtrl && !settings_modal_.IsOpen() &&
      (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
       ImGui::IsKeyPressed(ImGuiKey_Minus) ||
       ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
       ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) {
    float step = (ImGui::IsKeyPressed(ImGuiKey_Equal) ||
                  ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
                     ? 0.05f
                     : -0.05f;
    settings_.font_scale = std::round(settings_.font_scale / 0.05f) * 0.05f;
    settings_.font_scale = std::clamp(settings_.font_scale + step, 0.5f, 2.0f);
    settings_.Save();
    pending_style_ = std::make_tuple(settings_.style, settings_.layout,
                                     settings_.font_scale);
  }

  if (ImGui::GetIO().KeyCtrl && !settings_modal_.IsOpen() &&
      (ImGui::IsKeyPressed(ImGuiKey_0) ||
       ImGui::IsKeyPressed(ImGuiKey_Keypad0))) {
    settings_.font_scale = 1.0f;
    settings_.Save();
    pending_style_ = std::make_tuple(settings_.style, settings_.layout,
                                     settings_.font_scale);
  }

  if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q))
    platform_.Exit();
}

// -- CommitHistory::Delegate --------------------------------------------------

void MainWindow::OnCommitSelected(const std::string& commit,
                                  bool reselected,
                                  bool from_history) {
  blame_lookup_.OnCommitChanged(commit, reselected);
  commit_search_.Cancel();
  platform_.SetPrimarySelection(commit.c_str());
  toolbar_.SetCommitInput(commit);
  if (settings_.show_size_panel)
    commit_size_.SetCommit(commit);
  if (reselected) {
    commit_diff_.ScrollToTop();
    return;
  }

  // Remember where the diff being left was scrolled to, so navigating the
  // selection history back to it returns to the same place.
  commit_diff_.SaveScrollPosition();
  if (from_history) {
    commit_diff_.RestoreSavedScrollPosition(
        commit, commit_history_.IsUnstagedSelected(),
        commit_history_.IsStagedSelected());
  }
}

void MainWindow::OnRevertAllRequested() {
  commit_diff_.RevertAll();
}

// -- CommitDiff::Delegate -----------------------------------------------------

int MainWindow::GetSelectedCommitIndex() const {
  return commit_history_.GetSelectedCommitIndex();
}

void MainWindow::OnShowImageDiff(const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree) {
  image_diff_viewers_.push_back(std::make_unique<ImageDiffViewer>(
      next_image_diff_id_++, old_ref, new_ref, path, new_from_worktree));
}

void MainWindow::SetPrimarySelection(const std::string& text) {
  platform_.SetPrimarySelection(text.c_str());
}

bool MainWindow::IsUnstagedSelected() const {
  return commit_history_.IsUnstagedSelected();
}

bool MainWindow::IsStagedSelected() const {
  return commit_history_.IsStagedSelected();
}

void MainWindow::OnBlameSearchCancel() {
  blame_lookup_.Cancel();
}

void MainWindow::OnNavigateToCommit(const std::string& commit) {
  SelectCommitByHash(commit);
}

void MainWindow::OnRunGitGuiBlame(const std::string& file,
                                  int line_number,
                                  bool is_old_side) {
  std::string commit = commit_history_.GetSelectedCommit();
  std::string revision;
  if (commit_history_.IsUnstagedSelected() ||
      commit_history_.IsStagedSelected())
    revision = "HEAD";
  else
    revision = is_old_side ? (commit + "^") : commit;

  runner_.Spawn({"gui", "blame", "--line=" + std::to_string(line_number),
                 revision, file});
}

void MainWindow::OnShowLineOrigin(const std::string& file,
                                  int line_number,
                                  bool is_old_side) {
  blame_lookup_.ShowLineOrigin(file, line_number, is_old_side);
}

// -- Toolbar::Delegate --------------------------------------------------------

void MainWindow::SelectCommitByHash(std::string_view query) {
  if (query.empty())
    return;
  auto commits = git_log_.GetCommits();
  int synthetic_rows = commit_history_.SyntheticRows();
  for (size_t i = 0; i < commits.size(); i++) {
    if (commits[i].commit.starts_with(query)) {
      int row = static_cast<int>(i) + synthetic_rows;
      commit_history_.SelectCommit(row);
      commit_history_.ScrollToSelected();
      break;
    }
  }
}

int MainWindow::GetTotalRowCount() const {
  return static_cast<int>(git_log_.GetCommits().size()) +
         commit_history_.SyntheticRows();
}

int MainWindow::GetSelectedRowIndex() const {
  return commit_history_.GetSelectedRowIndex();
}

void MainWindow::UpdateLinesOfContext(int ctx) {
  if (ctx == git_diff_.GetLinesOfContext())
    return;
  git_diff_.SetLinesOfContext(ctx);
  settings_.context_lines = ctx;
  settings_.Save();
  RerunDiff(/*preserve_scroll=*/true);
}

void MainWindow::OnClearDiffPathFilterRequested() {
  if (git_diff_.HasPathFilter()) {
    git_diff_.ClearPathFilter();
    RerunDiff();
  }
}

void MainWindow::OnDiffRefreshRequested() {
  RerunDiff();
}

void MainWindow::OnRefreshRequested() {
  commit_history_.ResetScrollPos();
  Refresh();
}

void MainWindow::OnSettingsRequested() {
  if (fonts_.empty())
    fonts_ = platform_.GetFonts();
  settings_modal_.Open(settings_, renderer_->GetRendererType(), fonts_,
                       renderer_->GetAvailableGpus(),
                       renderer_->GetSelectedGpuIndex());
}

void MainWindow::OnHelpRequested() {
  help_modal_.Open();
}

bool MainWindow::HasDiffPathFilter() const {
  return git_diff_.HasPathFilter();
}

bool MainWindow::HasLogPathFilter() const {
  return git_log_.HasPathFilter();
}

void MainWindow::OnClearPathFilterRequested() {
  git_diff_.ClearPathFilter();
  git_status_.ClearPathFilter();
  if (git_log_.HasPathFilter()) {
    git_log_.ClearPathFilter();
    Refresh();
  } else {
    if (!browsing_specific_ref_)
      git_status_.Run();
    RerunDiff();
  }
}

void MainWindow::OnToggleLowerPanel() {
  settings_.show_size_panel = !settings_.show_size_panel;
  settings_.Save();
  if (settings_.show_size_panel) {
    int idx = commit_history_.GetSelectedCommitIndex();
    if (idx >= 0) {
      auto commits = git_log_.GetCommits();
      if (idx < static_cast<int>(commits.size()))
        commit_size_.SetCommit(commits[idx].commit);
    }
  } else if (diff_stale_) {
    diff_stale_ = false;
    RerunDiff();
  }
}

bool MainWindow::IsSizePanelActive() const {
  return settings_.show_size_panel;
}

bool MainWindow::IsUsingCSD() const {
  return using_csd_;
}

bool MainWindow::IsMaximized() const {
  return platform_.IsMaximized();
}

bool MainWindow::ShowMinimizeButton() const {
  return show_minimize_button_;
}

bool MainWindow::ShowMaximizeButton() const {
  return show_maximize_button_;
}

void MainWindow::OnMinimizeRequested() {
  platform_.Minimize();
}

void MainWindow::OnMaximizeToggleRequested() {
  platform_.SetMaximized(!platform_.IsMaximized());
}

void MainWindow::OnCloseRequested() {
  platform_.Exit();
}

void MainWindow::OnTitleBarDragRequested() {
  csd_interactive_ = true;
  platform_.BeginInteractiveMove();
#if defined(OS_WIN)
  csd_interactive_ = false;
#endif
}

void MainWindow::OnWindowMenuRequested(int x, int y) {
  platform_.ShowWindowMenu(x, y);
}

const std::string& MainWindow::GetWindowTitle() const {
  return window_title_;
}

void MainWindow::OnBranchSwitchRequested(const std::string& branch) {
  Exec proc;
  proc.Start({program_path_, branch});
  proc.Detach();
}

void MainWindow::OnConsoleWindowRequested() {
  console_window_.Toggle();
}

void MainWindow::OnSearchRequested(std::string term, int direction) {
  if (!commit_search_busy_) {
    commit_search_busy_ = true;
    on_busy_changed_(true);
  }
  commit_search_.Search(std::move(term),
                        commit_history_.GetSelectedCommitIndex(), direction,
                        settings_.search_options);
}

// -- DirectoryBrowser::Delegate -----------------------------------------------

void MainWindow::OnDirectorySelected(const std::filesystem::path& path) {
  std::filesystem::current_path(path);
  git_repo_.Init([this](bool found) {
    if (found) {
      Refresh();
    } else {
      directory_browser_.Open();
    }
  });
  directory_browser_.Close();
  UpdateWindowTitle({});
}

void MainWindow::OnCreateRepository(const std::filesystem::path& path) {
  std::filesystem::current_path(path);
  Exec proc;
  if (!proc.Start({"git", "init"}))
    return;
  while (proc.Poll()) {
  }
  if (proc.GetStatus() != Exec::Status::EXITED || proc.GetResult() != 0)
    return;
  git_repo_.Init([this](bool found) {
    DCHECK(found);
    Refresh();
  });
  directory_browser_.Close();
  UpdateWindowTitle({});
}

void MainWindow::OnBrowserDismissed() {
  platform_.Exit();
}
