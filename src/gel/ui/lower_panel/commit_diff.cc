// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/commit_diff.h"

#include <algorithm>
#include <cmath>

#include "gel/commands/git_diff.h"
#include "gel/persistent_settings.h"
#include "gel/ui/icons.h"
#include "gel/ui/style.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"

CommitDiff::CommitDiff(Delegate& delegate,
                       GitDiff& git_diff,
                       GitLog& git_log,
                       GitCmdRunner& runner,
                       PersistentSettings& settings,
                       FileList::ConfirmCallback on_confirm,
                       std::function<void()> main_thread_busy)
    : delegate_(delegate),
      settings_(settings),
      git_diff_(git_diff),
      file_list_(*this,
                 git_diff,
                 git_log,
                 runner,
                 settings,
                 std::move(on_confirm)),
      diff_content_(*this, git_diff, settings, std::move(main_thread_busy)) {}

int CommitDiff::GetSelectedCommitIndex() const {
  return delegate_.GetSelectedCommitIndex();
}

bool CommitDiff::IsUnstagedSelected() const {
  return delegate_.IsUnstagedSelected();
}

bool CommitDiff::IsStagedSelected() const {
  return delegate_.IsStagedSelected();
}

void CommitDiff::OnShowImageDiff(const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree) {
  delegate_.OnShowImageDiff(old_ref, new_ref, path, new_from_worktree);
}

void CommitDiff::SetPrimarySelection(const std::string& text) {
  delegate_.SetPrimarySelection(text);
}

void CommitDiff::OnShowLineOrigin(const std::string& file,
                                  int line_number,
                                  bool is_old_side) {
  delegate_.OnShowLineOrigin(file, line_number, is_old_side);
}

void CommitDiff::OnRunGitGuiBlame(const std::string& file,
                                  int line_number,
                                  bool is_old_side) {
  delegate_.OnRunGitGuiBlame(file, line_number, is_old_side);
}

void CommitDiff::OnBlameSearchCancel() {
  delegate_.OnBlameSearchCancel();
}

void CommitDiff::OnNavigateToCommit(const std::string& commit) {
  delegate_.OnNavigateToCommit(commit);
}

void CommitDiff::HighlightLine(int line) {
  diff_content_.HighlightLine(line);
}

void CommitDiff::Reset() {
  file_list_.Reset();
  diff_content_.Reset();
}

void CommitDiff::RevertAll() {
  file_list_.RevertAll();
}

void CommitDiff::ScrollToTop() {
  diff_content_.ScrollToTop();
  file_list_.SetSelectedFile(-1, true);
}

void CommitDiff::CaptureScrollAnchor() {
  diff_content_.CaptureScrollAnchor();
}

void CommitDiff::SaveScrollPosition() {
  diff_content_.SaveScrollPosition();
}

void CommitDiff::RestoreSavedScrollPosition(const std::string& commit,
                                            bool unstaged,
                                            bool staged) {
  diff_content_.RestoreSavedScrollPosition(commit, unstaged, staged);
}

void CommitDiff::CancelSearch() {
  diff_content_.CancelSearch();
}

void CommitDiff::RenderOverlayControls() {
  constexpr int kMaxContextLines = 99999;

  ImVec2 anchor = diff_content_.GetOverlayAnchor();
  if (anchor.x == 0 && anchor.y == 0)
    return;

  float frame_h = ImGui::GetFrameHeight();
  float spacing = ImGui::GetStyle().ItemSpacing.x;
  float ctx_input_w =
      ImGui::CalcTextSize("0000").x + ImGui::GetStyle().FramePadding.x * 2;
  float ctx_btn_w =
      ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2;

  bool show_refresh = IsUnstagedSelected() || IsStagedSelected();
  float total_width = (show_refresh ? frame_h + spacing : 0.0f) + frame_h +
                      spacing + ctx_input_w + ctx_btn_w;
  ImVec2 pad = ImGui::GetStyle().WindowPadding;
  pad.x *= 0.5f;
  pad.y *= 0.5f;
  ImVec2 pos(anchor.x - total_width - spacing - pad.x * 2, anchor.y + spacing);

  // Lightweight child container so widgets get hit testing.
  ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  bg.w = 1.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::BeginChild("##diff_overlay",
                    ImVec2(total_width + pad.x * 2, frame_h + pad.y * 2),
                    ImGuiChildFlags_None);

  // Center content within the padded frame
  ImGui::SetCursorPosY(pad.y);
  ImGui::SetCursorPosX(pad.x);

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    overlay_tooltip_suppressed_ = true;
  bool any_hovered = false;

  bool refresh_requested = false;
  if (show_refresh) {
    refresh_requested =
        ImGui::Button("##diff_refresh", ImVec2(frame_h, frame_h));
    DrawRefreshIcon();
    ItemTooltip("Refresh diff", any_hovered, overlay_tooltip_suppressed_);
    ImGui::SameLine();
  }
  bool has_filter = delegate_.HasDiffPathFilter();
  ImGui::BeginDisabled(!has_filter);
  bool clear_filter_requested =
      ImGui::Button("##diff_clear_filter", ImVec2(frame_h, frame_h));
  DrawClearFilterIcon();
  ItemTooltip("Clear diff path filter", any_hovered,
              overlay_tooltip_suppressed_);
  ImGui::EndDisabled();

  ImGui::SameLine();
  int ctx = settings_.context_lines;
  char ctx_buf[6];
  snprintf(ctx_buf, sizeof(ctx_buf), "%d", ctx);
  ImGui::BeginGroup();
  ImGui::SetNextItemWidth(ctx_input_w);
  if (ImGui::InputText(
          "##overlay_ctx", ctx_buf, sizeof(ctx_buf),
          ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll))
    ctx = atoi(ctx_buf);
  ImGui::SameLine(0, 0);
  float half_h = frame_h / 2;
  ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 dis_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  ImGui::BeginGroup();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginDisabled(ctx >= kMaxContextLines);
  if (ImGui::Button("##overlay_ctx_inc", ImVec2(ctx_btn_w, half_h)))
    ctx++;
  {
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 sz = ImGui::CalcTextSize("+");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(mn.x + (ctx_btn_w - sz.x) / 2, mn.y + (half_h - sz.y) / 2),
        ctx >= kMaxContextLines ? dis_col : text_col, "+");
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(ctx <= 0);
  if (ImGui::Button("##overlay_ctx_dec", ImVec2(ctx_btn_w, half_h)))
    ctx--;
  {
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 sz = ImGui::CalcTextSize("-");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(mn.x + (ctx_btn_w - sz.x) / 2, mn.y + (half_h - sz.y) / 2),
        ctx <= 0 ? dis_col : text_col, "-");
  }
  ImGui::EndDisabled();
  ImGui::PopStyleVar();
  ImGui::EndGroup();
  ImGui::EndGroup();
  ItemTooltip("Lines of context", any_hovered, overlay_tooltip_suppressed_);
  ctx = std::clamp(ctx, 0, kMaxContextLines);

  if (!any_hovered)
    overlay_tooltip_suppressed_ = false;

  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::EndChild();

  if (refresh_requested)
    delegate_.OnDiffRefreshRequested();
  if (clear_filter_requested)
    delegate_.OnClearDiffPathFilterRequested();
  delegate_.UpdateLinesOfContext(ctx);
}

void CommitDiff::Update(const std::string& search_term) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImGui::GetColorU32(ImGuiCol_WindowBg));
  bool lower_part_visible =
      ImGui::BeginChild("lower_part", ImVec2(-FLT_MIN, -FLT_MIN));
  ImGui::PopStyleColor();
  if (lower_part_visible) {
    float available = ImGui::GetContentRegionAvail().x;
    float thickness = GetSeparatorThickness() * ImGui::GetStyle()._MainScale;
    float max_file_list = available - thickness - 50.0f;
    if (max_file_list >= 50.0f)
      settings_.file_list_width =
          std::clamp(settings_.file_list_width, 50.0f, max_file_list);

    auto draw_separator = [&]() {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      float height = ImGui::GetContentRegionAvail().y;
      ImGui::InvisibleButton("##separator", ImVec2(thickness, height));
      bool hovered = ImGui::IsItemHovered();
      if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      ImU32 color = (hovered || ImGui::IsItemActive())
                        ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                        : ImGui::GetColorU32(ImGuiCol_WindowBg);
      float rounding = ImGui::GetStyle().ChildRounding;
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(pos.x, pos.y + rounding),
          ImVec2(pos.x + thickness, pos.y + height - rounding), color);
      if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        if (settings_.file_list_on_right)
          settings_.file_list_width -= delta;
        else
          settings_.file_list_width += delta;
        settings_.file_list_width =
            std::clamp(settings_.file_list_width, 50.0f, available - 50.0f);
      }
      ImGui::SameLine(0, 0);
    };

    float file_list_width =
        settings_.file_list_on_right ? -FLT_MIN : settings_.file_list_width;
    float diff_width = settings_.file_list_on_right
                           ? available - settings_.file_list_width - thickness
                           : -FLT_MIN;

    if (settings_.file_list_on_right) {
      diff_content_.Update(diff_width, search_term);
      ImGui::SameLine(0, 0);
      draw_separator();
      file_list_.Update(file_list_width);
    } else {
      file_list_.Update(file_list_width);
      ImGui::SameLine(0, 0);
      draw_separator();
      diff_content_.Update(diff_width, search_term);
    }

    // If the user clicked a file, scroll diff to that file's start line.
    int clicked = file_list_.ConsumeClickedFile();
    if (clicked >= 0) {
      auto files = git_diff_.GetFileList();
      if (clicked < static_cast<int>(files.size()))
        diff_content_.ScrollToLine(static_cast<int>(files[clicked].start_line));
    }

    // If auto-select picked a new file from scrolling, update the file list.
    // Skip when the user explicitly clicked a file this frame.
    int auto_file = diff_content_.ConsumeAutoSelectedFile();
    if (clicked < 0 && auto_file >= 0 &&
        auto_file != file_list_.selected_file())
      file_list_.SetSelectedFile(auto_file, true);

    RenderOverlayControls();
  }
  ImGui::EndChild();
}
