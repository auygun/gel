// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/lower_panel.h"

#include <algorithm>

#include "gel/persistent_settings.h"
#include "gel/ui/style.h"
#include "third_party/imgui/imgui/imgui.h"

namespace {

// The narrowest either pane can be dragged to.
constexpr float kMinPaneWidth = 50.0f;

}  // namespace

LowerPanel::LowerPanel(PersistentSettings& settings) : settings_(settings) {}

void LowerPanel::Update(View& view) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImGui::GetColorU32(ImGuiCol_WindowBg));
  bool visible = ImGui::BeginChild("lower_part", ImVec2(-FLT_MIN, -FLT_MIN));
  ImGui::PopStyleColor();
  if (visible) {
    float available = ImGui::GetContentRegionAvail().x;
    float thickness = GetSeparatorThickness() * ImGui::GetStyle()._MainScale;
    float max_side = available - thickness - kMinPaneWidth;
    if (max_side >= kMinPaneWidth)
      settings_.side_pane_width =
          std::clamp(settings_.side_pane_width, kMinPaneWidth, max_side);

    // The pane on the left gets a fixed width and the one on the right fills
    // the rest.
    if (settings_.side_pane_on_right) {
      view.RenderMainPane(available - settings_.side_pane_width - thickness);
      ImGui::SameLine(0, 0);
      RenderSeparator(available, thickness);
      view.RenderSidePane(-FLT_MIN);
    } else {
      view.RenderSidePane(settings_.side_pane_width);
      ImGui::SameLine(0, 0);
      RenderSeparator(available, thickness);
      view.RenderMainPane(-FLT_MIN);
    }

    view.FinishFrame();
  }
  ImGui::EndChild();
}

void LowerPanel::RenderSeparator(float available, float thickness) {
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
    if (settings_.side_pane_on_right)
      settings_.side_pane_width -= delta;
    else
      settings_.side_pane_width += delta;
    settings_.side_pane_width = std::clamp(
        settings_.side_pane_width, kMinPaneWidth, available - kMinPaneWidth);
  }
  ImGui::SameLine(0, 0);
}
