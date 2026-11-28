// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/toolbar.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>

#include "base/utf8.h"
#include "gel/commands/busy_tracker.h"
#include "gel/git_repo.h"
#include "gel/ui/git_cmd_runner.h"
#include "gel/ui/style.h"
#include "gel/ui/upper_panel/commit_context_menu.h"

#include "gel/persistent_settings.h"
#include "gel/ui/icons.h"
#include "gel/ui/modules/search_bar.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

Toolbar::Toolbar(Delegate& delegate,
                 GitCmdRunner& runner,
                 GitRepo& git_repo,
                 PersistentSettings& settings)
    : delegate_(delegate),
      runner_(runner),
      git_repo_(git_repo),
      settings_(settings) {}

void Toolbar::Update(const SearchBar::MatchState& search_matches,
                     float search_progress) {
  bool modal_open = ImGui::GetTopMostPopupModal() != nullptr;
  bool focus_search = false;

  // Tab cycles focus forward through the toolbar's input fields and, while it
  // is shown, the diff content's search field. Shift+Tab goes backward. With
  // none of them focused, Tab starts at the first and Shift+Tab at the last.
  if (!modal_open && !ImGui::GetIO().KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    std::vector<Field> fields = {Field::kCommit, Field::kSearch};
    if (delegate_.IsDiffSearchShown())
      fields.push_back(Field::kDiffSearch);
    int count = static_cast<int>(fields.size());
    auto it = std::find(fields.begin(), fields.end(), active_field_);
    int next;
    if (it == fields.end())
      next = ImGui::GetIO().KeyShift ? count - 1 : 0;
    else
      next = (static_cast<int>(it - fields.begin()) +
              (ImGui::GetIO().KeyShift ? count - 1 : 1)) %
             count;
    switch (fields[next]) {
      case Field::kCommit:
        focus_input_ = true;
        break;
      case Field::kSearch:
        focus_search = true;
        break;
      case Field::kDiffSearch:
        delegate_.FocusDiffSearch();
        break;
      case Field::kNone:
        break;
    }
  }
  if (focus_search)
    search_bar_.Focus();

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    tooltip_suppressed_ = true;
  bool any_toolbar_hovered = false;
  float frame_h = ImGui::GetFrameHeight();

  ImGui::SetNextItemWidth(
      ImGui::CalcTextSize("0000000000000000000000000000000000000000").x +
      ImGui::GetStyle().FramePadding.x * 2);
  bool input_entered = ImGui::InputText(
      "##commit_input", commit_input_, kCommitInputSize,
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
  InputTextContextMenu(commit_input_, kCommitInputSize);
  bool commit_input_active = ImGui::IsItemActive();
  ItemTooltip("Commit hash", any_toolbar_hovered, tooltip_suppressed_);
  if (commit_input_changed_) {
    commit_input_changed_ = false;
    snprintf(commit_input_, kCommitInputSize, "%s",
             pending_commit_input_.c_str());
    if (auto* state = ImGui::GetInputTextState(ImGui::GetItemID()))
      state->ReloadUserBufAndSelectAll();
  }
  if (focus_input_) {
    ImGui::SetKeyboardFocusHere(-1);
    focus_input_ = false;
  }

  // When the user presses Enter, search for a commit matching the typed hash.
  if (input_entered)
    delegate_.SelectCommitByHash(commit_input_);

  ImGui::SameLine();
  char row_buf[64];
  snprintf(row_buf, sizeof(row_buf), "%d/%d",
           delegate_.GetSelectedRowIndex() + 1, delegate_.GetTotalRowCount());
  ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000000/0000000").x +
                          ImGui::GetStyle().FramePadding.x * 2);
  ImVec4 frame_bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
  frame_bg.w *= ImGui::GetStyle().DisabledAlpha;
  ImGui::PushStyleColor(ImGuiCol_FrameBg, frame_bg);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frame_bg);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frame_bg);
  ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
  ImGui::InputText("##row_counter", row_buf, sizeof(row_buf),
                   ImGuiInputTextFlags_ReadOnly);
  ImGui::PopItemFlag();
  ImGui::PopStyleColor(3);
  ItemTooltip("Selected / total rows", any_toolbar_hovered,
              tooltip_suppressed_);

  ImGui::SameLine();

  bool refresh_requested =
      ImGui::Button("##refresh", ImVec2(frame_h, frame_h)) ||
      (!modal_open && ImGui::IsKeyPressed(ImGuiKey_F5));
  DrawRefreshIcon();
  ItemTooltip("Refresh (F5)", any_toolbar_hovered, tooltip_suppressed_);

  // "Clear path filter" button, disabled when no path filter is active.
  bool clear_filter_requested = false;
  {
    ImGui::SameLine();
    bool has_diff_filter = delegate_.HasDiffPathFilter();
    bool has_log_filter = delegate_.HasLogPathFilter();
    bool has_filter = has_diff_filter || has_log_filter;
    ImGui::BeginDisabled(!has_filter);
    clear_filter_requested =
        ImGui::Button("##clear_filter", ImVec2(frame_h, frame_h)) ||
        (!modal_open && has_filter && ImGui::GetIO().KeyCtrl &&
         ImGui::IsKeyPressed(ImGuiKey_L));
    DrawClearFilterIcon();
    ItemTooltip(has_diff_filter  ? "Clear diff path filter (Ctrl+L)"
                : has_log_filter ? "Clear log path filter (Ctrl+L)"
                                 : "Clear path filter (Ctrl+L)",
                any_toolbar_hovered, tooltip_suppressed_);
    ImGui::EndDisabled();
  }

  // Lower panel view buttons, one per view, the active one on. F6 cycles
  // through them.
  std::optional<LowerView> view_requested;
  {
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    struct ViewButton {
      LowerView view;
      const char* id;
      void (*draw_icon)();
      const char* tooltip;
    };
    static constexpr ViewButton kViewButtons[] = {
        {LowerView::kDiff, "##diff_view", DrawDiffPanelIcon, "Diff view (F6)"},
        {LowerView::kTree, "##tree_view", DrawTreePanelIcon,
         "File tree view (F6)"},
        {LowerView::kSize, "##size_view", DrawSizePanelIcon,
         "Commit size view (F6)"},
    };
    LowerView active = delegate_.GetLowerView();
    for (const auto& btn : kViewButtons) {
      ImGui::SameLine();
      if (ToggleButton(btn.id, btn.view == active, ImVec2(frame_h, frame_h)) &&
          btn.view != active)
        view_requested = btn.view;
      btn.draw_icon();
      ItemTooltip(btn.tooltip, any_toolbar_hovered, tooltip_suppressed_);
    }
    if (!modal_open && ImGui::IsKeyPressed(ImGuiKey_F6, false)) {
      constexpr int kViews = static_cast<int>(LowerView::kCount);
      view_requested =
          static_cast<LowerView>((static_cast<int>(active) + 1) % kViews);
    }
  }

  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  bool settings_requested =
      ImGui::Button("##settings", ImVec2(frame_h, frame_h)) ||
      (!modal_open && ImGui::GetIO().KeyCtrl &&
       ImGui::IsKeyPressed(ImGuiKey_Comma));
  DrawSettingsIcon();
  ItemTooltip("Settings (Ctrl+,)", any_toolbar_hovered, tooltip_suppressed_);

  ImGui::SameLine();
  bool help_requested = ImGui::Button("##help", ImVec2(frame_h, frame_h)) ||
                        (!modal_open && ImGui::IsKeyPressed(ImGuiKey_F1));
  DrawHelpIcon();
  ItemTooltip("Help (F1)", any_toolbar_hovered, tooltip_suppressed_);

  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();

  // What the commit search matches, as in gitk's find bar: an icon button
  // showing the current mode, opening a menu of the modes.
  {
    // Ctrl+M cycles to the next mode, Ctrl+Shift+M to the previous one. It
    // works while typing in the search field too, as it types no character.
    if (!modal_open && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_M, false)) {
      constexpr int kModes = static_cast<int>(CommitSearchMode::kCount);
      int step = ImGui::GetIO().KeyShift ? kModes - 1 : 1;
      search_mode_ = static_cast<CommitSearchMode>(
          (static_cast<int>(search_mode_) + step) % kModes);
      mode_flash_until_ = ImGui::GetTime() + kModeFlashSeconds;
    }

    ImVec2 button_size(frame_h, frame_h);
    if (ImGui::Button("##search_mode", button_size))
      ImGui::OpenPopup("##search_mode_menu");
    ImVec2 icon_min = ImGui::GetItemRectMin();
    ImVec2 icon_max = ImGui::GetItemRectMax();
    DrawSearchModeIcon(search_mode_, icon_min, icon_max);
    DrawMenuCaret();
    char tooltip[256];
    snprintf(tooltip, sizeof(tooltip),
             "Search mode: %s (Ctrl+M / Ctrl+Shift+M)\n%s",
             GetCommitSearchModeName(search_mode_),
             GetCommitSearchModeDescription(search_mode_));
    ItemTooltip(tooltip, any_toolbar_hovered, tooltip_suppressed_);

    // After the shortcut, name the new mode under the button for a moment:
    // the four icons are easy to mix up at a glance.
    if (ImGui::GetTime() < mode_flash_until_ &&
        !ImGui::IsPopupOpen("##search_mode_menu")) {
      ImGui::SetNextWindowPos(ImVec2(icon_min.x, icon_max.y + 2.0f));
      ImGui::Begin("##search_mode_flash", nullptr,
                   ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoDecoration |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoFocusOnAppearing |
                       ImGuiWindowFlags_NoNav);
      ImGui::TextUnformatted(GetCommitSearchModeName(search_mode_));
      ImGui::End();
      // Nothing else may ask for frames while the name is up.
      delegate_.OnKeepDrawingRequested();
    }

    ImGui::SetNextWindowPos(ImVec2(icon_min.x, icon_max.y));
    if (ImGui::BeginPopup("##search_mode_menu")) {
      // Icons as large as on the toolbar button; smaller, the "+/-" and ".*"
      // shapes are hard to tell apart.
      float icon_w = frame_h;
      float gap = ImGui::GetStyle().ItemInnerSpacing.x;
      float text_y = (icon_w - ImGui::GetTextLineHeight()) * 0.5f;
      float label_w = 0;
      for (int i = 0; i < static_cast<int>(CommitSearchMode::kCount); ++i) {
        label_w = std::max(
            label_w, ImGui::CalcTextSize(GetCommitSearchModeName(
                                             static_cast<CommitSearchMode>(i)))
                         .x);
      }
      for (int i = 0; i < static_cast<int>(CommitSearchMode::kCount); ++i) {
        auto mode = static_cast<CommitSearchMode>(i);
        ImGui::PushID(i);
        // The row is one selectable wide enough for the icon and the label,
        // which are drawn over it, so the highlight covers both.
        ImVec2 row = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##mode", mode == search_mode_, 0,
                              ImVec2(icon_w + gap + label_w, icon_w))) {
          search_mode_ = mode;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
          ImGui::SetTooltip("%s", GetCommitSearchModeDescription(mode));
        DrawSearchModeIcon(mode, row, ImVec2(row.x + icon_w, row.y + icon_w));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(row.x + icon_w + gap, row.y + text_y),
            ImGui::GetColorU32(ImGuiCol_Text), GetCommitSearchModeName(mode));
        ImGui::PopID();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::SameLine(0, 0);

  bool containing = search_mode_ == CommitSearchMode::kContaining;
  SearchBar::Options search_opts;
  search_opts.id = "search";
  search_opts.nav_enable = SearchBar::NavEnable::kHasTerm;
  search_opts.progress = search_progress;
  search_opts.arrow_shortcuts = !modal_open;
  search_opts.whole_word_disabled = !containing;
  // Shown in every mode, like whole word, so the buttons after it don't move
  // when switching modes.
  search_opts.regex = &settings_.search_regex;
  search_opts.regex_disabled =
      search_mode_ != CommitSearchMode::kAddingRemoving &&
      search_mode_ != CommitSearchMode::kChangingLines;
  switch (search_mode_) {
    case CommitSearchMode::kTouchingPaths:
      search_opts.input_tooltip = "Paths or globs, separated by spaces";
      break;
    case CommitSearchMode::kAddingRemoving:
      search_opts.input_tooltip =
          settings_.search_regex ? "Regex whose matches a commit changes"
                                 : "String whose occurrences a commit changes";
      break;
    case CommitSearchMode::kChangingLines:
      search_opts.input_tooltip =
          settings_.search_regex ? "Regex matching an added or removed line"
                                 : "String in an added or removed line";
      break;
    default:
      search_opts.input_tooltip = "Search in commits";
      break;
  }
  search_opts.tooltip = [&](const char* text) {
    ItemTooltip(text, any_toolbar_hovered, tooltip_suppressed_);
  };
  search_opts.on_input = [](char* buf, size_t buf_size) {
    InputTextContextMenu(buf, buf_size);
  };
  search_opts.history = &settings_.search_history;
  search_opts.idle_hint = "Type / to search";
  // Like the diff search, show the match counter and a button clearing the
  // search once there is something being searched for. The search bar is the
  // last item on the toolbar, so the counter can grow without moving anything
  // but the branch label at the far right.
  if (!search_bar_.empty()) {
    search_opts.match_counter = true;
    search_opts.matches = search_matches;
    search_opts.close_button = true;
    search_opts.close_tooltip = "Clear search";
  }
  SearchBar::Result search =
      search_bar_.Update(search_opts, &settings_.search_options);
  if (search.close_requested)
    search_bar_.SetTerm("");
  search_term_ = search_bar_.term();

  // '/' focuses the search field when no text input is active.  WantTextInput
  // covers inputs outside the toolbar (e.g. diff content search bar).
  if (!modal_open && !commit_input_active && !search.input_active &&
      !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyPressed(ImGuiKey_Slash, false))
    search_bar_.FocusRejectingChar('/');

  // Track which field is active for next frame's Tab handling. The diff
  // search is drawn after the toolbar, so this sees its state as of the last
  // frame, like the toolbar's own fields at the next Tab press.
  if (commit_input_active)
    active_field_ = Field::kCommit;
  else if (search.input_active)
    active_field_ = Field::kSearch;
  else if (delegate_.IsDiffSearchFocused())
    active_field_ = Field::kDiffSearch;
  else
    active_field_ = Field::kNone;

  if (!any_toolbar_hovered)
    tooltip_suppressed_ = false;

  // Right edge of the last toolbar widget (window-local X coordinate).
  float widgets_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                      ImGui::GetStyle().ItemSpacing.x;

  // Display current branch/HEAD and in-progress task status,
  // right-justified on the toolbar line.
  {
    auto task = runner_.task();
    bool has_task = task != GitTask::kNone;
    std::string display_head = head_label_;

    // Status badge: a dot (yellow while a task is in progress, gray
    // otherwise). Clicking the dot or pressing F7 opens the git console.
    constexpr float kDotSize = 12.0f;
    float badge_w = kDotSize;

    // Busy spinner in front of the dot while git commands run in the
    // background. Its tooltip lists them.
    bool git_busy = BusyTracker::Get().busy();
    float spinner_gap = ImGui::GetStyle().ItemSpacing.x;
    // The spinner and the gap after it.
    float spinner_w = git_busy ? frame_h + spinner_gap : 0.0f;

    if (has_task || !display_head.empty()) {
      float right_edge = ImGui::GetWindowContentRegionMax().x;
      float available =
          right_edge - widgets_end - ImGui::GetStyle().ItemSpacing.x;
      float status_w = spinner_w + badge_w + ImGui::GetStyle().ItemSpacing.x;

      // Truncate the branch name with ellipsis when the combined text
      // would overlap the toolbar widgets. Hide everything when the
      // branch name no longer fits.
      bool show_badge = true;
      if (!display_head.empty()) {
        float total_w = status_w + ImGui::CalcTextSize(display_head.c_str()).x;
        if (total_w > available) {
          float avail_head = available - status_w;
          float ellipsis_w = ImGui::CalcTextSize("...").x;
          if (avail_head >= ellipsis_w) {
            display_head = "...";
            // Advance by full UTF-8 sequences so a multi-byte character is
            // never cut in half (which would render as a replacement glyph).
            for (size_t i = 0; i < head_label_.size();) {
              i += base::Utf8SequenceLength(head_label_, i);
              std::string trial = head_label_.substr(0, i) + "...";
              if (ImGui::CalcTextSize(trial.c_str()).x > avail_head)
                break;
              display_head = std::move(trial);
            }
          } else {
            display_head.clear();
            show_badge = false;
            status_w = 0;
          }
        }
      }

      float text_width = status_w + ImGui::CalcTextSize(display_head.c_str()).x;
      bool console_key = !modal_open && ImGui::IsKeyPressed(ImGuiKey_F7);
      if (text_width > 0) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(right_edge - text_width);
        if (show_badge && git_busy) {
          ImGui::InvisibleButton("##git_busy", ImVec2(frame_h, frame_h));
          DrawBusySpinner(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
          // Identical commands (e.g. the commit search running git diff-tree
          // in several processes) are listed once, with a count.
          std::vector<std::pair<std::string, int>> counted;
          for (auto& command : BusyTracker::Get().Commands()) {
            auto it = std::find_if(
                counted.begin(), counted.end(),
                [&](const auto& entry) { return entry.first == command; });
            if (it != counted.end())
              ++it->second;
            else
              counted.emplace_back(std::move(command), 1);
          }
          std::string commands = "Running in the background:";
          for (const auto& [command, count] : counted) {
            commands += '\n';
            commands += command;
            if (count > 1)
              commands += " (x" + std::to_string(count) + ")";
          }
          ItemTooltip(commands.c_str(), any_toolbar_hovered,
                      tooltip_suppressed_);
          ImGui::SameLine(0, spinner_gap);
        }
        if (show_badge) {
          ImVec2 badge_min = ImGui::GetCursorScreenPos();
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
          bool clicked = ImGui::InvisibleButton(
              "##git_console", ImVec2(badge_w, ImGui::GetFrameHeight()));
          ImGui::PopStyleColor();
          if (clicked || console_key)
            delegate_.OnConsoleWindowRequested();
          bool hovered = ImGui::IsItemHovered();
          ImU32 dot_color =
              has_task ? ResolveColor(hovered ? ColorId::kBrightYellow
                                              : ColorId::kYellow)
                       : ResolveColor(hovered ? ColorId::kDefault
                                              : ColorId::kTextDisabled);
          ImGui::GetWindowDrawList()->AddCircleFilled(
              ImVec2(badge_min.x + kDotSize * 0.5f,
                     badge_min.y + frame_h * 0.5f),
              kDotSize * 0.5f, dot_color);
          char tooltip[128];
          if (has_task)
            snprintf(tooltip, sizeof(tooltip), "%s in progress (F7)",
                     GetTaskName(task));
          else
            snprintf(tooltip, sizeof(tooltip), "Console (F7)");
          ItemTooltip(tooltip, any_toolbar_hovered, tooltip_suppressed_);
        }
        if (!display_head.empty()) {
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Text, ResolveColor(ColorId::kGreen));
          ImGui::TextUnformatted(display_head.c_str());
          ImGui::PopStyleColor();
          if (ImGui::BeginPopupContextItem("##branch_ctx")) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
              ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##branch_esc"),
                                 ImGuiInputFlags_LockThisFrame);
              ImGui::CloseCurrentPopup();
            }
            for (auto& branch : local_branches_) {
              bool is_current = (branch == head_label_);
              if (ImGui::BeginMenu(branch.c_str())) {
                if (is_current) {
                  ImGui::PushStyleColor(ImGuiCol_Text,
                                        ResolveColor(ColorId::kGreen));
                  ImGui::TextUnformatted("* current branch");
                  ImGui::PopStyleColor();
                  ImGui::Separator();
                }
                RenderBranchMenuItems(
                    runner_, branch, false, is_current, branch_rename_input_,
                    sizeof(branch_rename_input_), branch_create_input_,
                    sizeof(branch_create_input_),
                    [this](const std::string& b) {
                      delegate_.OnBranchSwitchRequested(b);
                    },
                    [this](std::string_view title, std::string message,
                           std::function<void()> on_confirm) {
                      delegate_.OnConfirmationRequested(
                          title, std::move(message), std::move(on_confirm));
                    });
                ImGui::EndMenu();
              }
            }
            ImGui::EndPopup();
          }
          ItemTooltip(head_detached_
                          ? "Detached HEAD\nRight-click for options"
                          : "Current branch\nRight-click for options",
                      any_toolbar_hovered, tooltip_suppressed_);
        }
      }
    }
  }

  if (refresh_requested)
    delegate_.OnRefreshRequested();
  if (clear_filter_requested)
    delegate_.OnClearPathFilterRequested();
  if (settings_requested)
    delegate_.OnSettingsRequested();
  if (help_requested)
    delegate_.OnHelpRequested();
  if (view_requested)
    delegate_.OnLowerViewRequested(*view_requested);
  if (search.next || search.prev)
    delegate_.OnSearchRequested(search.next ? 1 : -1);
}

void Toolbar::SetSearch(const std::string& term, CommitSearchMode mode) {
  search_bar_.SetTerm(term);
  // Also right away, so a search started this frame sees it.
  search_term_ = search_bar_.term();
  search_mode_ = mode;
}

void Toolbar::SetCommitInput(const std::string& commit) {
  snprintf(commit_input_, kCommitInputSize, "%s", commit.c_str());
  pending_commit_input_ = commit_input_;
  commit_input_changed_ = true;
}

void Toolbar::UpdateHeadStatus() {
  namespace fs = std::filesystem;

  auto& git_dir = git_repo_.git_dir();
  if (git_dir.empty())
    return;

  head_label_.clear();
  head_detached_ = false;
  std::ifstream head_file(git_dir / "HEAD");
  if (head_file) {
    std::string line;
    std::getline(head_file, line);
    constexpr std::string_view kBranchPrefix = "ref: refs/heads/";
    constexpr std::string_view kRefPrefix = "ref: ";
    if (line.starts_with(kBranchPrefix)) {
      head_label_ = line.substr(kBranchPrefix.size());
    } else if (line.starts_with(kRefPrefix)) {
      head_label_ = line.substr(kRefPrefix.size());
    } else {
      head_detached_ = true;
      if (line.size() >= 7)
        head_label_ = line.substr(0, 7);
      else
        head_label_ = line;
    }
  }

  // Resolve the common git directory (for worktree support).
  if (common_git_dir_.empty()) {
    auto commondir_file = git_dir / "commondir";
    std::error_code ec;
    if (fs::exists(commondir_file, ec)) {
      std::ifstream f(commondir_file);
      std::string line;
      std::getline(f, line);
      common_git_dir_ = fs::canonical(git_dir / line, ec);
      if (ec)
        common_git_dir_ = git_dir;
    } else {
      common_git_dir_ = git_dir;
    }
  }

  // Enumerate local branches from refs/heads/ and packed-refs.
  local_branches_.clear();
  auto heads_dir = common_git_dir_ / "refs" / "heads";
  std::error_code ec;
  if (fs::is_directory(heads_dir, ec)) {
    for (auto& entry : fs::recursive_directory_iterator(heads_dir, ec)) {
      if (entry.is_regular_file())
        local_branches_.push_back(
            fs::relative(entry.path(), heads_dir).generic_string());
    }
  }
  auto packed_refs = common_git_dir_ / "packed-refs";
  if (fs::exists(packed_refs, ec)) {
    std::ifstream f(packed_refs);
    std::string line;
    constexpr std::string_view kHeadsPrefix = "refs/heads/";
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#' || line[0] == '^')
        continue;
      auto space = line.find(' ');
      if (space == std::string::npos)
        continue;
      std::string ref = line.substr(space + 1);
      if (ref.starts_with(kHeadsPrefix)) {
        std::string branch(ref.substr(kHeadsPrefix.size()));
        if (std::find(local_branches_.begin(), local_branches_.end(), branch) ==
            local_branches_.end())
          local_branches_.push_back(std::move(branch));
      }
    }
  }
  std::sort(local_branches_.begin(), local_branches_.end());
}
