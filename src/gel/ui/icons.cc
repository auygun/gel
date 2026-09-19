// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/icons.h"

#include <algorithm>
#include <cmath>

#include "third_party/imgui/imgui/imgui.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void DrawRefreshIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float radius = size * 0.28f;
  float thickness = std::max(1.5f, radius * 0.25f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float arrow_size = radius * 0.55f;

  // Arc with gap at the top, shortened so the arrowhead continues
  // smoothly from the arc end.
  float gap_half = kPi / 6.0f;
  float arc_start = -kPi * 0.5f + gap_half;
  float arrow_angular = arrow_size / radius;
  float arc_end = -kPi * 0.5f - gap_half + 2.0f * kPi - arrow_angular;
  dl->PathArcTo(center, radius, arc_start, arc_end, 24);
  dl->PathStroke(color, 0, thickness);

  // Arrowhead whose tip and base both follow the arc curvature.
  // Tip sits at the original (unshortened) arc endpoint on the circle.
  // Base straddles the circle at the shortened arc endpoint.
  float tip_angle = arc_end + arrow_angular;
  ImVec2 tip(center.x + radius * cosf(tip_angle),
             center.y + radius * sinf(tip_angle));
  float base_angle = arc_end;
  float half_w = radius * 0.4f;
  ImVec2 outer(center.x + (radius + half_w) * cosf(base_angle),
               center.y + (radius + half_w) * sinf(base_angle));
  ImVec2 inner(center.x + (radius - half_w) * cosf(base_angle),
               center.y + (radius - half_w) * sinf(base_angle));
  dl->AddTriangleFilled(tip, outer, inner, color);
}

void DrawSettingsIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.07f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

  // Inner circle (hole of the gear).
  float inner_r = size * 0.12f;
  dl->AddCircle(center, inner_r, color, 0, thickness);

  // Outer gear body: circle with protruding teeth.
  float outer_r = size * 0.24f;
  float tooth_r = size * 0.34f;
  constexpr int num_teeth = 8;
  constexpr float tooth_half = kPi / (num_teeth * 2.0f);

  // Build the gear outline as a polygon.
  dl->PathClear();
  constexpr int segs_per_section = 4;
  for (int i = 0; i < num_teeth; i++) {
    float tooth_center_angle =
        (static_cast<float>(i) / num_teeth) * 2.0f * kPi - kPi * 0.5f;

    // Valley arc (between teeth) on the outer circle.
    float valley_start = tooth_center_angle + tooth_half;
    float valley_end =
        tooth_center_angle + (2.0f * kPi / num_teeth) - tooth_half;
    for (int s = 0; s <= segs_per_section; s++) {
      float t = static_cast<float>(s) / segs_per_section;
      float a = valley_start + t * (valley_end - valley_start);
      dl->PathLineTo(
          ImVec2(center.x + outer_r * cosf(a), center.y + outer_r * sinf(a)));
    }

    // Tooth arc on the tooth circle.
    float next_center = tooth_center_angle + (2.0f * kPi / num_teeth);
    float tooth_start = next_center - tooth_half;
    float tooth_end = next_center + tooth_half;
    for (int s = 0; s <= segs_per_section; s++) {
      float t = static_cast<float>(s) / segs_per_section;
      float a = tooth_start + t * (tooth_end - tooth_start);
      dl->PathLineTo(
          ImVec2(center.x + tooth_r * cosf(a), center.y + tooth_r * sinf(a)));
    }
  }
  dl->PathStroke(color, ImDrawFlags_Closed, thickness);
}

void DrawCaseSensitiveIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Capital "A" on the left half.
  float a_top = cy - size * 0.28f;
  float a_bot = cy + size * 0.28f;
  float a_cx = cx - size * 0.17f;
  float a_half_w = size * 0.16f;
  float a_bar_y = cy + size * 0.08f;
  // Left leg.
  dl->AddLine(ImVec2(a_cx - a_half_w, a_bot), ImVec2(a_cx, a_top), color,
              thickness);
  // Right leg.
  dl->AddLine(ImVec2(a_cx, a_top), ImVec2(a_cx + a_half_w, a_bot), color,
              thickness);
  // Crossbar.
  float bar_frac = (a_bar_y - a_top) / (a_bot - a_top);
  float bar_half = a_half_w * bar_frac;
  dl->AddLine(ImVec2(a_cx - bar_half, a_bar_y),
              ImVec2(a_cx + bar_half, a_bar_y), color, thickness);

  // Lowercase "a" on the right half: circle + vertical stem.
  float a2_cx = cx + size * 0.17f;
  float a2_r = size * 0.13f;
  float a2_top = cy;
  float a2_bot = cy + size * 0.28f;
  float a2_cy = a2_bot - a2_r;
  dl->AddCircle(ImVec2(a2_cx, a2_cy), a2_r, color, 0, thickness);
  dl->AddLine(ImVec2(a2_cx + a2_r, a2_top), ImVec2(a2_cx + a2_r, a2_bot), color,
              thickness);
}

namespace {

// Shared body for the navigation arrows. |up| picks the direction.
void DrawArrowIcon(bool enabled, bool up) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = (btn_max.y - btn_min.y) * 0.3f;
  ImU32 color =
      ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);

  float tip = up ? -size : size;
  float base = up ? size * 0.6f : -size * 0.6f;
  dl->AddTriangleFilled(ImVec2(center.x, center.y + tip),
                        ImVec2(center.x - size, center.y + base),
                        ImVec2(center.x + size, center.y + base), color);
}

}  // namespace

void DrawArrowUpIcon(bool enabled) {
  DrawArrowIcon(enabled, true);
}

void DrawArrowDownIcon(bool enabled) {
  DrawArrowIcon(enabled, false);
}

void DrawWholeWordIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Boundary ticks. Letterforms are avoided on purpose: an "ab" next to the
  // "Aa" of the case toggle is two glyph pairs that are hard to tell apart at
  // the size these buttons are drawn.
  float tick_half_h = size * 0.26f;
  float tick_x = size * 0.30f;
  dl->AddLine(ImVec2(cx - tick_x, cy - tick_half_h),
              ImVec2(cx - tick_x, cy + tick_half_h), color, thickness);
  dl->AddLine(ImVec2(cx + tick_x, cy - tick_half_h),
              ImVec2(cx + tick_x, cy + tick_half_h), color, thickness);

  // The word between them, as a solid slug.
  dl->AddRectFilled(ImVec2(cx - size * 0.17f, cy - size * 0.09f),
                    ImVec2(cx + size * 0.17f, cy + size * 0.09f), color,
                    size * 0.05f);
}

void DrawClearFilterIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Funnel shape: wide top narrowing to a stem at the bottom.
  float top_y = cy - size * 0.28f;
  float mid_y = cy + size * 0.02f;
  float bot_y = cy + size * 0.28f;
  float top_half = size * 0.30f;
  float mid_half = size * 0.06f;

  // Left side of funnel.
  dl->AddLine(ImVec2(cx - top_half, top_y), ImVec2(cx - mid_half, mid_y), color,
              thickness);
  // Right side of funnel.
  dl->AddLine(ImVec2(cx + top_half, top_y), ImVec2(cx + mid_half, mid_y), color,
              thickness);
  // Top bar.
  dl->AddLine(ImVec2(cx - top_half, top_y), ImVec2(cx + top_half, top_y), color,
              thickness);
  // Stem.
  dl->AddLine(ImVec2(cx, mid_y), ImVec2(cx, bot_y), color, thickness);

  // "X" overlay in the lower-right corner.
  float x_cx = cx + size * 0.20f;
  float x_cy = cy + size * 0.16f;
  float x_sz = size * 0.12f;
  ImU32 x_color = IM_COL32(220, 60, 60, 255);
  float x_thick = std::max(2.0f, size * 0.10f);
  dl->AddLine(ImVec2(x_cx - x_sz, x_cy - x_sz),
              ImVec2(x_cx + x_sz, x_cy + x_sz), x_color, x_thick);
  dl->AddLine(ImVec2(x_cx + x_sz, x_cy - x_sz),
              ImVec2(x_cx - x_sz, x_cy + x_sz), x_color, x_thick);
}

void DrawDiffPanelIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Colored horizontal lines resembling diff output.
  ImU32 green = IM_COL32(0x0D, 0xBC, 0x79, 0xFF);
  ImU32 red = IM_COL32(0xDC, 0x3C, 0x3C, 0xFF);
  float half_w = size * 0.28f;
  float spacing = size * 0.13f;
  // Green line (long).
  dl->AddLine(ImVec2(cx - half_w, cy - spacing),
              ImVec2(cx + half_w, cy - spacing), green, thickness);
  // Red line (medium).
  dl->AddLine(ImVec2(cx - half_w, cy), ImVec2(cx + half_w * 0.5f, cy), red,
              thickness);
  // Green line (short).
  dl->AddLine(ImVec2(cx - half_w, cy + spacing),
              ImVec2(cx + half_w * 0.75f, cy + spacing), green, thickness);
}

void DrawSizePanelIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Vertical bar chart.
  constexpr float heights[] = {0.50f, 1.00f, 0.35f, 0.75f};
  constexpr int num_bars = 4;
  float total_w = size * 0.50f;
  float bar_w = total_w / (num_bars * 2 - 1);
  float max_h = size * 0.40f;
  float base_y = cy + max_h * 0.5f;
  float start_x = cx - total_w * 0.5f;
  for (int i = 0; i < num_bars; i++) {
    float x0 = start_x + i * bar_w * 2;
    float x1 = x0 + bar_w;
    float y0 = base_y - heights[i] * max_h;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, base_y), color);
  }
}

void DrawDonutChartIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

  // Ring (circle with a hole).
  float outer_r = size * 0.32f;
  float inner_r = size * 0.17f;
  dl->AddCircle(center, outer_r, color, 0, thickness);
  dl->AddCircle(center, inner_r, color, 0, thickness);
}

void DrawBarChartIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Horizontal bars of varying widths.
  constexpr float widths[] = {1.0f, 0.6f, 0.8f};
  constexpr int num_bars = 3;
  float max_w = size * 0.52f;
  float bar_h = size * 0.12f;
  float gap = size * 0.06f;
  float total_h = num_bars * bar_h + (num_bars - 1) * gap;
  float start_y = cy - total_h * 0.5f;
  float left_x = cx - max_w * 0.5f;

  for (int i = 0; i < num_bars; i++) {
    float y0 = start_y + i * (bar_h + gap);
    float x1 = left_x + widths[i] * max_w;
    dl->AddRectFilled(ImVec2(left_x, y0), ImVec2(x1, y0 + bar_h), color);
  }
}

void DrawTreemapIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Rectangle subdivided into smaller rectangles.
  float half = size * 0.30f;
  float x0 = cx - half, y0 = cy - half;
  float x1 = cx + half, y1 = cy + half;
  // Outer rect.
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, 0, 0, thickness);
  // Vertical split at ~60%.
  float vx = x0 + (x1 - x0) * 0.6f;
  dl->AddLine(ImVec2(vx, y0), ImVec2(vx, y1), color, thickness);
  // Horizontal split in right portion at ~50%.
  float hy = y0 + (y1 - y0) * 0.5f;
  dl->AddLine(ImVec2(vx, hy), ImVec2(x1, hy), color, thickness);
  // Horizontal split in left portion at ~65%.
  float hy2 = y0 + (y1 - y0) * 0.65f;
  dl->AddLine(ImVec2(x0, hy2), ImVec2(vx, hy2), color, thickness);
}

void DrawSortByNameIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // "A" letter on the left.
  float a_top = cy - size * 0.26f;
  float a_bot = cy + size * 0.26f;
  float a_cx = cx - size * 0.14f;
  float a_half_w = size * 0.12f;
  float a_bar_y = cy + size * 0.08f;
  dl->AddLine(ImVec2(a_cx - a_half_w, a_bot), ImVec2(a_cx, a_top), color,
              thickness);
  dl->AddLine(ImVec2(a_cx, a_top), ImVec2(a_cx + a_half_w, a_bot), color,
              thickness);
  float bar_frac = (a_bar_y - a_top) / (a_bot - a_top);
  float bar_half = a_half_w * bar_frac;
  dl->AddLine(ImVec2(a_cx - bar_half, a_bar_y),
              ImVec2(a_cx + bar_half, a_bar_y), color, thickness);

  // "Z" letter on the right.
  float z_cx = cx + size * 0.14f;
  float z_half_w = size * 0.10f;
  dl->AddLine(ImVec2(z_cx - z_half_w, a_top), ImVec2(z_cx + z_half_w, a_top),
              color, thickness);
  dl->AddLine(ImVec2(z_cx + z_half_w, a_top), ImVec2(z_cx - z_half_w, a_bot),
              color, thickness);
  dl->AddLine(ImVec2(z_cx - z_half_w, a_bot), ImVec2(z_cx + z_half_w, a_bot),
              color, thickness);
}

void DrawSortBySizeIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Horizontal bars decreasing in width from top to bottom.
  constexpr float widths[] = {1.0f, 0.7f, 0.4f};
  constexpr int num_bars = 3;
  float max_w = size * 0.52f;
  float bar_h = size * 0.12f;
  float gap = size * 0.06f;
  float total_h = num_bars * bar_h + (num_bars - 1) * gap;
  float start_y = cy - total_h * 0.5f;
  float left_x = cx - max_w * 0.5f;

  for (int i = 0; i < num_bars; i++) {
    float y0 = start_y + i * (bar_h + gap);
    float x1 = left_x + widths[i] * max_w;
    dl->AddRectFilled(ImVec2(left_x, y0), ImVec2(x1, y0 + bar_h), color);
  }
}

void DrawCollapseAllIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Three horizontal lines at decreasing indent to suggest collapsing.
  float half_w = size * 0.26f;
  float step = size * 0.16f;
  float indent = size * 0.12f;
  dl->AddLine(ImVec2(cx - half_w, cy - step), ImVec2(cx + half_w, cy - step),
              color, thickness);
  dl->AddLine(ImVec2(cx - half_w + indent, cy), ImVec2(cx + half_w, cy), color,
              thickness);
  dl->AddLine(ImVec2(cx - half_w + indent, cy + step),
              ImVec2(cx + half_w, cy + step), color, thickness);

  // Upward arrow on the left indicating collapse.
  float arrow_x = cx - half_w + indent * 0.5f;
  float arrow_sz = size * 0.08f;
  dl->AddTriangleFilled(ImVec2(arrow_x, cy - arrow_sz * 0.5f),
                        ImVec2(arrow_x - arrow_sz, cy + arrow_sz * 0.5f),
                        ImVec2(arrow_x + arrow_sz, cy + arrow_sz * 0.5f),
                        color);
}

void DrawFolderIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float w = btn_max.x - btn_min.x;
  float h = btn_max.y - btn_min.y;
  float size = std::min(w, h);
  float thickness = std::max(1.5f, size * 0.08f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  float half_w = size * 0.42f;
  float half_h = size * 0.32f;
  float tab_w = size * 0.22f;
  float tab_h = size * 0.10f;

  // Folder body.
  float x0 = cx - half_w, y0 = cy - half_h + tab_h;
  float x1 = cx + half_w, y1 = cy + half_h;
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, 0, 0, thickness);

  // Tab on the upper-left corner.
  dl->AddLine(ImVec2(x0, y0), ImVec2(x0, y0 - tab_h), color, thickness);
  dl->AddLine(ImVec2(x0, y0 - tab_h), ImVec2(x0 + tab_w, y0 - tab_h), color,
              thickness);
  dl->AddLine(ImVec2(x0 + tab_w, y0 - tab_h), ImVec2(x0 + tab_w, y0), color,
              thickness);
}

void DrawGitFolderIcon() {
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Git logo: rotated rounded square (diamond) with a Y-shaped branch inside.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 git_color = IM_COL32(0xF0, 0x50, 0x33, 0xFF);
  float s = size * 0.50f;

  // Diamond (45-degree rotated square).
  dl->AddNgonFilled(ImVec2(cx, cy + size * 0.04f), s, git_color, 4);

  // Y-shaped branch path inside the diamond.
  ImU32 white = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
  float line_t = std::max(1.0f, size * 0.05f);
  float dot_r = size * 0.04f;
  float logo_cy = cy + size * 0.04f;
  ImVec2 center(cx, logo_cy);
  ImVec2 bottom(cx, logo_cy + s * 0.50f);
  ImVec2 top_left(cx - s * 0.32f, logo_cy - s * 0.32f);
  ImVec2 top_right(cx + s * 0.32f, logo_cy - s * 0.32f);
  dl->AddLine(bottom, center, white, line_t);
  dl->AddLine(center, top_left, white, line_t);
  dl->AddLine(center, top_right, white, line_t);
  dl->AddCircleFilled(bottom, dot_r, white);
  dl->AddCircleFilled(top_left, dot_r, white);
  dl->AddCircleFilled(top_right, dot_r, white);
}

void DrawGitFirstIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Miniature git logo: orange diamond with white Y-branch.
  ImU32 git_color = IM_COL32(0xF0, 0x50, 0x33, 0xFF);
  float s = size * 0.38f;
  dl->AddNgonFilled(ImVec2(cx, cy), s, git_color, 4);

  ImU32 white = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
  float line_t = std::max(1.0f, size * 0.05f);
  float dot_r = size * 0.035f;
  ImVec2 bottom(cx, cy + s * 0.50f);
  ImVec2 center(cx, cy);
  ImVec2 top_left(cx - s * 0.32f, cy - s * 0.32f);
  ImVec2 top_right(cx + s * 0.32f, cy - s * 0.32f);
  dl->AddLine(bottom, center, white, line_t);
  dl->AddLine(center, top_left, white, line_t);
  dl->AddLine(center, top_right, white, line_t);
  dl->AddCircleFilled(bottom, dot_r, white);
  dl->AddCircleFilled(top_left, dot_r, white);
  dl->AddCircleFilled(top_right, dot_r, white);
}

void DrawAppIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.09f);

  // Blue filled circle background.
  float radius = size * 0.38f;
  dl->AddCircleFilled(center, radius, IM_COL32(0x2A, 0x6B, 0xC6, 0xFF));

  // White "G" shape: arc covering most of the circle with a gap in the
  // upper right, plus a horizontal bar from the right edge inward.
  ImU32 fg = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
  float g_r = size * 0.22f;
  float arc_start = 0;                      // 3 o'clock
  float arc_end = 2.0f * kPi - kPi / 3.0f;  // ~300°, stops at ~1 o'clock
  dl->PathArcTo(center, g_r, arc_start, arc_end, 24);
  dl->PathStroke(fg, 0, thickness);
  // Horizontal bar from the right edge of the arc toward the center.
  dl->AddLine(ImVec2(center.x, center.y), ImVec2(center.x + g_r, center.y), fg,
              thickness);
}

void DrawMinimizeIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.09f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Horizontal line at the vertical center.
  float half_w = size * 0.22f;
  dl->AddLine(ImVec2(cx - half_w, cy), ImVec2(cx + half_w, cy), color,
              thickness);
}

void DrawMaximizeIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.09f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Square outline.
  float half = size * 0.20f;
  dl->AddRect(ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half), color,
              0, 0, thickness);
}

void DrawRestoreIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.09f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // Two overlapping squares: front (lower-left) and back (upper-right).
  float half = size * 0.16f;
  float offset = size * 0.08f;
  // Front square.
  dl->AddRect(ImVec2(cx - half - offset, cy - half + offset),
              ImVec2(cx + half - offset, cy + half + offset), color, 0, 0,
              thickness);
  // Back square (only visible edges: top and right).
  float bx0 = cx - half + offset, by0 = cy - half - offset;
  float bx1 = cx + half + offset, by1 = cy + half - offset;
  dl->AddLine(ImVec2(bx0, by0), ImVec2(bx1, by0), color, thickness);
  dl->AddLine(ImVec2(bx1, by0), ImVec2(bx1, by1), color, thickness);
  dl->AddLine(ImVec2(bx0, by0), ImVec2(bx0, cy - half + offset), color,
              thickness);
  dl->AddLine(ImVec2(cx + half - offset, by1), ImVec2(bx1, by1), color,
              thickness);
}

void DrawCloseIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(1.5f, size * 0.09f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  float cx = (btn_min.x + btn_max.x) * 0.5f;
  float cy = (btn_min.y + btn_max.y) * 0.5f;

  // X mark.
  float half = size * 0.20f;
  dl->AddLine(ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half), color,
              thickness);
  dl->AddLine(ImVec2(cx + half, cy - half), ImVec2(cx - half, cy + half), color,
              thickness);
}

void DrawHelpIcon() {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 btn_min = ImGui::GetItemRectMin();
  ImVec2 btn_max = ImGui::GetItemRectMax();
  ImVec2 center((btn_min.x + btn_max.x) * 0.5f, (btn_min.y + btn_max.y) * 0.5f);
  float size = std::min(btn_max.x - btn_min.x, btn_max.y - btn_min.y);
  float thickness = std::max(2.0f, size * 0.09f);
  ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

  // Question mark: 270-degree arc (opening on the lower-left) plus a stem.
  float radius = size * 0.16f;
  ImVec2 arc_center(center.x, center.y - size * 0.1f);

  // Arc from left, over top, down the right, to the bottom.
  dl->PathArcTo(arc_center, radius, -kPi, kPi * 0.5f, 20);
  // Stem straight down from the bottom of the arc.
  dl->PathLineTo(ImVec2(center.x, center.y + size * 0.12f));
  dl->PathStroke(color, 0, thickness);

  // Round caps at the start and end of the stroke.
  float cap = thickness * 0.5f;
  dl->AddCircleFilled(ImVec2(arc_center.x - radius, arc_center.y), cap, color);
  dl->AddCircleFilled(ImVec2(center.x, center.y + size * 0.12f), cap, color);

  // Dot.
  float dot_r = thickness * 0.85f;
  dl->AddCircleFilled(ImVec2(center.x, center.y + size * 0.27f), dot_r, color);
}
