// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/toolbar.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "base/utf8.h"
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

void Toolbar::Update(float search_progress) {
  bool modal_open = ImGui::GetTopMostPopupModal() != nullptr;
  bool focus_search = false;

  // Tab cycles focus between toolbar input fields, Shift+Tab goes backward.
  // 0 = commit input, 1 = search.
  if (!modal_open && !ImGui::GetIO().KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
    if (ImGui::GetIO().KeyShift) {
      if (active_field_ == 0)
        focus_search = true;
      else if (active_field_ == 1)
        focus_input_ = true;
      else
        focus_search = true;
    } else {
      if (active_field_ == 0)
        focus_search = true;
      else if (active_field_ == 1)
        focus_input_ = true;
      else
        focus_input_ = true;
    }
  }
  if (focus_search)
    search_bar_.Focus();

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    tooltip_suppressed_ = true;
  bool any_toolbar_hovered = false;
  float frame_h = ImGui::GetFrameHeight();

  // CSD window control buttons at the far right.
  bool csd = delegate_.IsUsingCSD();
  bool show_minimize = csd && delegate_.ShowMinimizeButton();
  bool show_maximize = csd && delegate_.ShowMaximizeButton();
  bool minimize_requested = false;
  bool maximize_toggle_requested = false;
  bool close_requested = false;
  float csd_buttons_width = 0;
  if (csd) {
    // Calculate total width of CSD buttons so toolbar items can leave room.
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    int button_count = 1 + (show_minimize ? 1 : 0) + (show_maximize ? 1 : 0);
    csd_buttons_width = frame_h * button_count + spacing * button_count;

    // Clip toolbar items so they don't overlap the CSD buttons.
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wpad = ImGui::GetStyle().WindowPadding;
    float clip_right =
        wpos.x + ImGui::GetWindowContentRegionMax().x - csd_buttons_width;
    ImGui::PushClipRect(ImVec2(wpos.x, wpos.y),
                        ImVec2(clip_right, wpos.y + frame_h + wpad.y * 2),
                        true);
  }

  // Draw the app icon at the left edge when CSD is active.
  // Right-click shows the system window menu.  Drawn on the foreground draw
  // list so it stays visible above modals.
  bool window_menu_requested = false;
  if (csd) {
    csd_app_icon_min_ = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(frame_h, frame_h));
    csd_app_icon_max_ = ImGui::GetItemRectMax();
    {
      ImDrawList* dl = ImGui::GetForegroundDrawList();
      ImVec2 bmin = csd_app_icon_min_, bmax = csd_app_icon_max_;
      ImVec2 center((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f);
      float sz = std::min(bmax.x - bmin.x, bmax.y - bmin.y);
      float thick = std::max(1.5f, sz * 0.09f);
      float radius = sz * 0.38f;
      dl->AddCircleFilled(center, radius, IM_COL32(0x2A, 0x6B, 0xC6, 0xFF));
      ImU32 fg = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
      float g_r = sz * 0.22f;
      float arc_start = 0;
      float arc_end = 2.0f * 3.14159265f - 3.14159265f / 3.0f;
      dl->PathArcTo(center, g_r, arc_start, arc_end, 24);
      dl->PathStroke(fg, 0, thick);
      dl->AddLine(ImVec2(center.x, center.y), ImVec2(center.x + g_r, center.y),
                  fg, thick);
    }
    ImGui::SameLine();
  }

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

  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  SearchBar::Options search_opts;
  search_opts.id = "search";
  search_opts.nav_enable = SearchBar::NavEnable::kHasTerm;
  search_opts.progress = search_progress;
  search_opts.arrow_shortcuts = !modal_open;
  search_opts.input_tooltip = "Search in commits";
  search_opts.tooltip = [&](const char* text) {
    ItemTooltip(text, any_toolbar_hovered, tooltip_suppressed_);
  };
  search_opts.on_input = [](char* buf, size_t buf_size) {
    InputTextContextMenu(buf, buf_size);
  };
  SearchBar::Result search =
      search_bar_.Update(search_opts, &settings_.search_options);
  search_term_ = search_bar_.term();

  // '/' focuses the search field when no text input is active.  WantTextInput
  // covers inputs outside the toolbar (e.g. diff content search bar).
  if (!modal_open && !commit_input_active && !search.input_active &&
      !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyPressed(ImGuiKey_Slash, false))
    search_bar_.FocusRejectingChar('/');

  // Track which field is active for next frame's Tab handling.
  if (commit_input_active)
    active_field_ = 0;
  else if (search.input_active)
    active_field_ = 1;
  else
    active_field_ = -1;

  // Lower panel toggle button.
  bool toggle_panel_requested = false;
  {
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    bool size_active = delegate_.IsSizePanelActive();
    toggle_panel_requested =
        ToggleButton("##panel_toggle", size_active, ImVec2(frame_h, frame_h)) ||
        (!modal_open && ImGui::IsKeyPressed(ImGuiKey_F6));
    if (size_active)
      DrawSizePanelIcon();
    else
      DrawDiffPanelIcon();
    ItemTooltip(
        size_active ? "Show diff view (F6)" : "Show commit size view (F6)",
        any_toolbar_hovered, tooltip_suppressed_);
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

  if (!any_toolbar_hovered)
    tooltip_suppressed_ = false;

  // Right edge of the last toolbar widget (window-local X coordinate).
  // Used to position the window title in the CSD drag area.
  float widgets_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                      ImGui::GetStyle().ItemSpacing.x;

  // Display current branch/HEAD and in-progress task status,
  // right-justified on the toolbar line (leaving room for CSD buttons).
  float right_content_start =
      ImGui::GetWindowContentRegionMax().x - csd_buttons_width;
  {
    auto task = runner_.task();
    bool has_task = task != GitTask::kNone;
    std::string display_head = head_label_;

    // Status badge: a dot (yellow while a task is in progress, gray
    // otherwise). Clicking the dot or pressing F7 opens the git console.
    constexpr float kDotSize = 12.0f;
    float badge_w = kDotSize;

    if (has_task || !display_head.empty()) {
      float right_edge =
          ImGui::GetWindowContentRegionMax().x - csd_buttons_width;
      float available =
          right_edge - widgets_end - ImGui::GetStyle().ItemSpacing.x;
      float status_w = badge_w + ImGui::GetStyle().ItemSpacing.x;

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
                    sizeof(branch_create_input_), [this](const std::string& b) {
                      delegate_.OnBranchSwitchRequested(b);
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
        right_content_start = right_edge - text_width;
      }
    }
  }

  // Draw the window title centered in the drag area when CSD is active.
  if (csd) {
    auto& title = delegate_.GetWindowTitle();
    if (!title.empty()) {
      float title_width = ImGui::CalcTextSize(title.c_str()).x;
      float gap_start = widgets_end;
      float gap_end = right_content_start;
      float gap_width = gap_end - gap_start;
      if (gap_width > 0) {
        float title_x = gap_start + (gap_width - title_width) / 2;
        // Clamp so the title doesn't overlap toolbar widgets or right content.
        title_x = std::max(title_x, gap_start);
        ImVec2 window_pos = ImGui::GetWindowPos();
        float title_y = window_pos.y + ImGui::GetStyle().WindowPadding.y;
        auto* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(ImVec2(window_pos.x + gap_start, title_y),
                         ImVec2(window_pos.x + gap_end, title_y + frame_h));
        bool focused =
            delegate_.IsWindowFocused() ||
            ImGui::IsPopupOpen(
                "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        ImGuiCol title_col = focused ? ImGuiCol_Text : ImGuiCol_TextDisabled;
        dl->AddText(
            ImVec2(window_pos.x + title_x,
                   title_y + (frame_h - ImGui::GetTextLineHeight()) / 2),
            ImGui::GetColorU32(title_col), title.c_str());
        dl->PopClipRect();
      }
    }
  }

  // Pop the toolbar clip rect before drawing CSD buttons.
  if (csd)
    ImGui::PopClipRect();

  // CSD window control buttons.  Drawn on the foreground draw list with raw
  // mouse input so they always work, even when a modal is open.
  if (csd) {
    float right_edge = ImGui::GetWindowContentRegionMax().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    int button_count = 1 + (show_minimize ? 1 : 0) + (show_maximize ? 1 : 0);
    ImGui::SameLine();
    float start_x =
        right_edge - frame_h * button_count - spacing * (button_count - 1);
    ImGui::SetCursorPosX(start_x);

    // Reserve layout space.
    float total_w = frame_h * button_count + spacing * (button_count - 1);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(total_w, frame_h));

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 mpos = ImGui::GetMousePos();

    auto hit = [&mpos](ImVec2 bmin, ImVec2 bmax) {
      return mpos.x >= bmin.x && mpos.x < bmax.x && mpos.y >= bmin.y &&
             mpos.y < bmax.y;
    };

    // Build button rects.
    struct BtnInfo {
      CSDButton id;
      ImVec2 bmin, bmax;
    };
    BtnInfo btns[3];
    int btn_count = 0;
    ImVec2 cur = origin;
    if (show_minimize) {
      btns[btn_count++] = {
          CSDButton::kMinimize, cur, {cur.x + frame_h, cur.y + frame_h}};
      cur.x += frame_h + spacing;
    }
    if (show_maximize) {
      btns[btn_count++] = {
          CSDButton::kMaximize, cur, {cur.x + frame_h, cur.y + frame_h}};
      cur.x += frame_h + spacing;
    }
    btns[btn_count++] = {
        CSDButton::kClose, cur, {cur.x + frame_h, cur.y + frame_h}};

    // Press detection.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      csd_pressed_ = CSDButton::kNone;
      for (int i = 0; i < btn_count; ++i) {
        if (hit(btns[i].bmin, btns[i].bmax)) {
          csd_pressed_ = btns[i].id;
          break;
        }
      }
    }

    // Draw backgrounds with hover/press colors.
    bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImU32 col_btn = ImGui::GetColorU32(ImGuiCol_Button);
    ImU32 col_hov = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
    ImU32 col_act = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    ImU32 col_close_hov = ResolveColor(ColorId::kRed);
    ImU32 col_close_act = ResolveColor(ColorId::kBrightRed);
    float rounding = ImGui::GetStyle().FrameRounding;

    for (int i = 0; i < btn_count; ++i) {
      bool hovered = hit(btns[i].bmin, btns[i].bmax);
      bool pressed = hovered && mouse_down && csd_pressed_ == btns[i].id;
      bool is_close = btns[i].id == CSDButton::kClose;
      ImU32 bg;
      if (pressed)
        bg = is_close ? col_close_act : col_act;
      else if (hovered)
        bg = is_close ? col_close_hov : col_hov;
      else
        bg = col_btn;
      dl->AddRectFilled(btns[i].bmin, btns[i].bmax, bg, rounding);
    }

    // Draw icons.
    ImU32 icon_col = ImGui::GetColorU32(ImGuiCol_Text);
    for (int i = 0; i < btn_count; ++i) {
      ImVec2 bmin = btns[i].bmin, bmax = btns[i].bmax;
      float sz = std::min(bmax.x - bmin.x, bmax.y - bmin.y);
      float thick = std::max(1.5f, sz * 0.09f);
      float cx = (bmin.x + bmax.x) * 0.5f, cy = (bmin.y + bmax.y) * 0.5f;
      switch (btns[i].id) {
        case CSDButton::kMinimize: {
          float hw = sz * 0.22f;
          dl->AddLine(ImVec2(cx - hw, cy), ImVec2(cx + hw, cy), icon_col,
                      thick);
          break;
        }
        case CSDButton::kMaximize:
          if (delegate_.IsMaximized()) {
            float half = sz * 0.16f, off = sz * 0.08f;
            dl->AddRect(ImVec2(cx - half - off, cy - half + off),
                        ImVec2(cx + half - off, cy + half + off), icon_col, 0,
                        0, thick);
            float bx0 = cx - half + off, by0 = cy - half - off;
            float bx1 = cx + half + off, by1 = cy + half - off;
            dl->AddLine(ImVec2(bx0, by0), ImVec2(bx1, by0), icon_col, thick);
            dl->AddLine(ImVec2(bx1, by0), ImVec2(bx1, by1), icon_col, thick);
            dl->AddLine(ImVec2(bx0, by0), ImVec2(bx0, cy - half + off),
                        icon_col, thick);
            dl->AddLine(ImVec2(cx + half - off, by1), ImVec2(bx1, by1),
                        icon_col, thick);
          } else {
            float half = sz * 0.20f;
            dl->AddRect(ImVec2(cx - half, cy - half),
                        ImVec2(cx + half, cy + half), icon_col, 0, 0, thick);
          }
          break;
        case CSDButton::kClose: {
          float half = sz * 0.20f;
          dl->AddLine(ImVec2(cx - half, cy - half),
                      ImVec2(cx + half, cy + half), icon_col, thick);
          dl->AddLine(ImVec2(cx + half, cy - half),
                      ImVec2(cx - half, cy + half), icon_col, thick);
          break;
        }
        default:
          break;
      }
    }

    // Release detection.
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      for (int i = 0; i < btn_count; ++i) {
        if (csd_pressed_ == btns[i].id && hit(btns[i].bmin, btns[i].bmax)) {
          switch (btns[i].id) {
            case CSDButton::kMinimize:
              minimize_requested = true;
              break;
            case CSDButton::kMaximize:
              maximize_toggle_requested = true;
              break;
            case CSDButton::kClose:
              close_requested = true;
              break;
            default:
              break;
          }
        }
      }
      csd_pressed_ = CSDButton::kNone;
    }

    // App icon right-click for window menu.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        hit(csd_app_icon_min_, csd_app_icon_max_)) {
      window_menu_requested = true;
    }
  }

  // CSD drag detection: limit the interactive area to the window title region
  // (the gap between the last toolbar widget and the right-side content).
  // Double-click toggles maximize/restore; single-click initiates window drag.
  if (csd) {
    ImVec2 window_pos = ImGui::GetWindowPos();
    float title_min_x = window_pos.x + widgets_end;
    float title_max_x = window_pos.x + right_content_start;
    float title_min_y = window_pos.y;
    float title_max_y =
        window_pos.y + frame_h + ImGui::GetStyle().WindowPadding.y * 2;
    ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x >= title_min_x && mouse.x < title_max_x &&
        mouse.y >= title_min_y && mouse.y < title_max_y) {
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        maximize_toggle_requested = true;
      else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        delegate_.OnTitleBarDragRequested();
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
  if (toggle_panel_requested)
    delegate_.OnToggleLowerPanel();
  if (search.next || search.prev)
    delegate_.OnSearchRequested(search_term_, search.next ? 1 : -1);
  if (minimize_requested)
    delegate_.OnMinimizeRequested();
  if (maximize_toggle_requested)
    delegate_.OnMaximizeToggleRequested();
  if (close_requested)
    delegate_.OnCloseRequested();
  if (window_menu_requested)
    delegate_.OnWindowMenuRequested(static_cast<int>(csd_app_icon_min_.x),
                                    static_cast<int>(csd_app_icon_max_.y));
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
