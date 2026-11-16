// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/overlay_toolbar.h"

#include <cmath>

#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

namespace {

// Between the toolbar's frame and its content.
ImVec2 Padding() {
  ImVec2 pad = ImGui::GetStyle().WindowPadding;
  return ImVec2(pad.x * 0.5f, pad.y * 0.5f);
}

}  // namespace

float OverlayToolbar::ButtonSize() {
  return ImGui::GetFrameHeight();
}

float OverlayToolbar::HalfButtonWidth() {
  return ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2;
}

float OverlayToolbar::Spacing() {
  return ImGui::GetStyle().ItemSpacing.x;
}

float OverlayToolbar::SeparatorWidth() {
  return 1.0f;
}

float OverlayToolbar::BottomOffset() {
  return Spacing() + ButtonSize() + Padding().y * 2;
}

float OverlayToolbar::CenterOffset() {
  return Spacing() + Padding().y + ButtonSize() / 2;
}

ImVec2 OverlayToolbar::WindowAnchor() {
  ImVec2 pos = ImGui::GetWindowPos();
  return ImVec2(pos.x + ImGui::GetWindowSize().x, pos.y);
}

float OverlayToolbar::Width(float content_width) {
  return content_width + Padding().x * 2;
}

void OverlayToolbar::Begin(const char* id,
                           const ImVec2& anchor,
                           float content_width) {
  float spacing = Spacing();
  ImVec2 pad = Padding();
  float width = Width(content_width);
  ImVec2 pos(anchor.x - width - spacing, anchor.y + spacing);

  // Lightweight child container so widgets get hit testing. It paints its own
  // background: ImGui may draw a child's background into its parent's draw
  // list, which is rendered below all the parent's children, so the frame
  // would end up under the panel it overlays and only the buttons on top.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImVec2 size(width, ButtonSize() + pad.y * 2);
  ImGui::BeginChild(id, size, ImGuiChildFlags_None);
  ImGui::PopStyleColor();

  ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  bg.w = 1.0f;
  ImGui::GetWindowDrawList()->AddRectFilled(
      pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(bg),
      ImGui::GetStyle().ChildRounding);

  // Center content within the padded frame.
  ImGui::SetCursorPos(pad);

  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    tooltip_suppressed_ = true;
  any_hovered_ = false;
}

void OverlayToolbar::End() {
  if (!any_hovered_)
    tooltip_suppressed_ = false;

  ImGui::PopStyleVar();
  ImGui::EndChild();
}

void OverlayToolbar::Tooltip(const char* text) {
  ItemTooltip(text, any_hovered_, tooltip_suppressed_);
}

void OverlayToolbar::Separator() {
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, SeparatorWidth());
}

void OverlayToolbar::ExpandCollapseButtons(bool& expand, bool& collapse) {
  float btn_w = HalfButtonWidth();
  float half_h = ButtonSize() / 2;
  ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);

  auto draw_arrow = [&](bool down) {
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    float r = std::floor(half_h * 0.3f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (down) {
      dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.5f),
                            ImVec2(c.x + r, c.y - r * 0.5f),
                            ImVec2(c.x, c.y + r * 0.5f), text_col);
    } else {
      dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y - r),
                            ImVec2(c.x + r * 0.5f, c.y),
                            ImVec2(c.x - r * 0.5f, c.y + r), text_col);
    }
  };

  ImGui::BeginGroup();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  if (ImGui::Button("##expand_all", ImVec2(btn_w, half_h)))
    expand = true;
  draw_arrow(true);
  Tooltip("Expand all");
  if (ImGui::Button("##collapse_all", ImVec2(btn_w, half_h)))
    collapse = true;
  draw_arrow(false);
  Tooltip("Collapse all");
  ImGui::PopStyleVar();
  ImGui::EndGroup();
}
