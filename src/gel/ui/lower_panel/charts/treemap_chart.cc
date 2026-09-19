// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/charts/treemap_chart.h"

#include <algorithm>
#include <cmath>

#include "third_party/imgui/imgui/imgui.h"

void TreemapChart::Render(const std::vector<SliceInfo>& slices) {
  int64_t total = delegate_.GetTotalSize();
  int deferred_drill_down = -1;
  bool deferred_navigate_parent = false;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 origin = ImGui::GetCursorScreenPos();
  float total_area = avail.x * avail.y;

  // Squarified treemap layout.
  struct Rect {
    float x, y, w, h;
  };
  int n = static_cast<int>(slices.size());
  std::vector<Rect> rects(n);

  // Layout state.
  float rx = origin.x, ry = origin.y, rw = avail.x, rh = avail.y;

  auto layout_row = [&](int start, int end, float row_area) {
    if (rw <= 0 || rh <= 0 || row_area <= 0)
      return;
    bool horizontal = (rw >= rh);
    float side = horizontal ? rh : rw;
    float row_length = row_area / side;

    float offset = 0;
    for (int i = start; i < end; i++) {
      float item_size =
          slices[i].fraction * total_area / (row_length > 0 ? row_length : 1);
      item_size = std::min(item_size, side - offset);
      if (item_size < 0)
        item_size = 0;

      if (horizontal) {
        rects[i] = {rx, ry + offset, row_length, item_size};
      } else {
        rects[i] = {rx + offset, ry, item_size, row_length};
      }
      offset += item_size;
    }

    if (horizontal) {
      rx += row_length;
      rw -= row_length;
    } else {
      ry += row_length;
      rh -= row_length;
    }
  };

  // Squarify algorithm.
  if (n > 0) {
    int row_start = 0;
    float row_area = 0;

    auto worst_aspect = [&](int start, int end, float area) -> float {
      if (area <= 0)
        return 1e9f;
      bool horizontal = (rw >= rh);
      float side = horizontal ? rh : rw;
      float row_length = area / side;
      if (row_length <= 0)
        return 1e9f;

      float worst = 0;
      for (int i = start; i < end; i++) {
        float item_size = slices[i].fraction * total_area / row_length;
        float aspect = (item_size > row_length) ? item_size / row_length
                                                : row_length / item_size;
        worst = std::max(worst, aspect);
      }
      return worst;
    };

    for (int i = 0; i < n; i++) {
      float new_area = row_area + slices[i].fraction * total_area;
      if (i == row_start) {
        row_area = new_area;
        continue;
      }

      float old_worst = worst_aspect(row_start, i, row_area);
      float new_worst = worst_aspect(row_start, i + 1, new_area);

      if (new_worst <= old_worst) {
        row_area = new_area;
      } else {
        layout_row(row_start, i, row_area);
        row_start = i;
        row_area = slices[i].fraction * total_area;
      }
    }
    if (row_start < n)
      layout_row(row_start, n, row_area);
  }

  // Draw rectangles.
  auto* dl = ImGui::GetWindowDrawList();
  ImVec2 mouse = ImGui::GetMousePos();
  int hovered_item = -1;
  float gap = 1.0f;

  for (int i = 0; i < n; i++) {
    auto& r = rects[i];
    ImVec2 rmin(r.x + gap, r.y + gap);
    ImVec2 rmax(r.x + r.w - gap, r.y + r.h - gap);
    if (rmax.x <= rmin.x || rmax.y <= rmin.y)
      continue;

    bool hovered = (mouse.x >= rmin.x && mouse.x < rmax.x &&
                    mouse.y >= rmin.y && mouse.y < rmax.y);
    if (hovered)
      hovered_item = i;

    bool is_others = (slices[i].child_index < 0);
    ImU32 col = is_others ? (hovered ? IM_COL32(0x99, 0x99, 0x99, 0xFF)
                                     : IM_COL32(0x77, 0x77, 0x77, 0xFF))
                          : (hovered ? SliceColorLight(i) : SliceColor(i));
    dl->AddRectFilled(rmin, rmax, col);

    // Text label inside if large enough.
    float rect_w = rmax.x - rmin.x;
    float rect_h = rmax.y - rmin.y;
    float text_h = ImGui::GetTextLineHeight();
    float padding = 3.0f;
    if (rect_w > 30 && rect_h > text_h + padding * 2) {
      const char* name =
          is_others ? "Others"
                    : delegate_.GetChildName(slices[i].child_index).c_str();
      ImVec2 text_pos(rmin.x + padding, rmin.y + padding);
      dl->PushClipRect(rmin, rmax, true);
      dl->AddText(text_pos, IM_COL32(255, 255, 255, 230), name);

      // Size label on second line if there's room.
      if (rect_h > text_h * 2 + padding * 2) {
        std::string sz = HumanSize(slices[i].size);
        ImVec2 sz_pos(rmin.x + padding, rmin.y + padding + text_h);
        dl->AddText(sz_pos, IM_COL32(255, 255, 255, 170), sz.c_str());
      }
      dl->PopClipRect();
    }

    // Hover border.
    if (hovered) {
      ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
      bool light_bg = (bg.x * 0.299f + bg.y * 0.587f + bg.z * 0.114f) > 0.5f;
      ImU32 border_col =
          light_bg ? IM_COL32(0, 0, 0, 200) : IM_COL32(255, 255, 255, 200);
      dl->AddRect(rmin, rmax, border_col, 0, 0, 2.0f);
    }
  }

  // Reserve space.
  ImGui::Dummy(avail);

  // Tooltip and click handling.
  if (hovered_item >= 0) {
    auto& slice = slices[hovered_item];
    if (slice.child_index >= 0) {
      int ci = slice.child_index;
      float real_pct = 100.0f * static_cast<float>(delegate_.GetChildSize(ci)) /
                       static_cast<float>(total);
      ImGui::SetTooltip("%s%s\n%s (%.1f%%)", delegate_.GetChildName(ci).c_str(),
                        delegate_.IsChildDirectory(ci) ? "/" : "",
                        HumanSize(delegate_.GetChildSize(ci)).c_str(),
                        real_pct);
      if (delegate_.IsChildDirectory(ci) &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        deferred_drill_down = ci;
      }
    } else {
      float real_pct =
          100.0f * static_cast<float>(slice.size) / static_cast<float>(total);
      ImGui::SetTooltip("Others (%d items)\n%s (%.1f%%)", slice.others_count,
                        HumanSize(slice.size).c_str(), real_pct);
    }
  }

  // Right-click to go to parent.
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    deferred_navigate_parent = true;
  }

  // Execute deferred navigation after all rendering is complete, since
  // DrillDown/NavigateToParent change the delegate's state and would
  // invalidate child indices used by the drawing loop above.
  if (deferred_drill_down >= 0)
    delegate_.DrillDown(deferred_drill_down);
  else if (deferred_navigate_parent)
    delegate_.NavigateToParent();
}
