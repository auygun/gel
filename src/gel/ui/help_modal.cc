// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/help_modal.h"

#include "gel/ui/icons.h"
#include "gel/ui/utils.h"
#include "help_data.h"
#include "licenses_data.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

HelpModal::HelpModal() = default;

void HelpModal::Open() {
  show_ = true;
  active_tab_ = 0;
  select_tab_ = true;
}

void HelpModal::Update(bool window_focused) {
  if (show_) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 modal_size(display.x * 0.7f, display.y * 0.8f);
    ImGui::SetNextWindowSize(modal_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.15f, display.y * 0.1f),
                            ImGuiCond_Appearing);
    ImGui::OpenPopup("Help");
    show_ = false;
    open_ = true;
  }
  if (!window_focused)
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
  bool was_open = ImGui::IsPopupOpen("Help");
  bool stay_open = true;
  if (ImGui::BeginPopupModal("Help", &stay_open)) {
    if (!renderers_[0].parsed())
      renderers_[0].Parse(kHelpData, sizeof(kHelpData) - 1);
    if (!renderers_[1].parsed())
      renderers_[1].Parse(kLicensesData, sizeof(kLicensesData) - 1);

    MarkdownRenderer& renderer = renderers_[active_tab_];

    // Left/Right arrows switch tabs.
    if (!ImGui::GetIO().WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && active_tab_ > 0) {
        active_tab_--;
        select_tab_ = true;
      } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) &&
                 active_tab_ < kTabCount - 1) {
        active_tab_++;
        select_tab_ = true;
      }
    }

    // Escape closes the modal, unless the renderer's search bar or context
    // menu has a claim on it. Both close themselves on Escape; checked here
    // before the renderer runs, so the state is the one the key applies to.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !renderer.context_menu_open() &&
        !renderer.search_active()) {
      ImGui::CloseCurrentPopup();
      open_ = false;
    }

    // Up/Down heading navigation buttons (right-aligned on the tab bar row).
    // The work rect is shrunk so tab labels won't overlap the buttons.
    float heading_btn_w =
        ImGui::CalcTextSize("W").x + ImGui::GetStyle().FramePadding.x * 2;
    {
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        heading_tooltip_suppressed_ = true;
      bool any_hovered = false;
      float frame_h = ImGui::GetFrameHeight();
      float half_h = frame_h / 2;
      ImVec2 saved_cursor = ImGui::GetCursorPos();
      float avail_w = ImGui::GetContentRegionAvail().x;
      ImGui::SetCursorPosX(saved_cursor.x + avail_w - heading_btn_w);
      ImGui::BeginGroup();
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
      if (ImGui::Button("##help_heading_up", ImVec2(heading_btn_w, half_h)))
        renderer.ScrollToHeading(-1);
      DrawArrowUpIcon(true);
      ItemTooltip("Previous heading (Shift+Tab)", any_hovered,
                  heading_tooltip_suppressed_);
      if (ImGui::Button("##help_heading_down", ImVec2(heading_btn_w, half_h)))
        renderer.ScrollToHeading(1);
      DrawArrowDownIcon(true);
      ItemTooltip("Next heading (Tab)", any_hovered,
                  heading_tooltip_suppressed_);
      ImGui::PopStyleVar();
      ImGui::EndGroup();
      ImGui::SetCursorPos(saved_cursor);
      if (!any_hovered)
        heading_tooltip_suppressed_ = false;
    }
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    float saved_work_rect_max_x = win->WorkRect.Max.x;
    win->WorkRect.Max.x -= heading_btn_w + ImGui::GetStyle().ItemSpacing.x;

    // Tab bar.
    if (ImGui::BeginTabBar("##help_tabs",
                           ImGuiTabBarFlags_NoTabListScrollingButtons)) {
      if (ImGui::BeginTabItem("Help", nullptr,
                              (select_tab_ && active_tab_ == 0)
                                  ? ImGuiTabItemFlags_SetSelected
                                  : 0)) {
        if (!select_tab_)
          active_tab_ = 0;
        renderers_[0].Render("##help_content",
                             ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Licenses", nullptr,
                              (select_tab_ && active_tab_ == 1)
                                  ? ImGuiTabItemFlags_SetSelected
                                  : 0)) {
        if (!select_tab_)
          active_tab_ = 1;
        renderers_[1].Render("##licenses_content",
                             ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
      select_tab_ = false;
    }

    win->WorkRect.Max.x = saved_work_rect_max_x;

    float button_width = ImGui::GetFontSize() * 6;
    if (ImGui::Button("Close", ImVec2(button_width, 0))) {
      ImGui::CloseCurrentPopup();
      open_ = false;
    }

    ImGui::EndPopup();
  }
  // The title bar's close button dismisses the popup before the body runs.
  if (was_open && !stay_open)
    open_ = false;
  if (!window_focused)
    ImGui::PopStyleColor();
}
