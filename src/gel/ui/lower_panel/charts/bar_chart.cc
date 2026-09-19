// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/charts/bar_chart.h"

#include <algorithm>

#include "third_party/imgui/imgui/imgui.h"

void BarChart::Render(const std::vector<SliceInfo>& slices) {
  int64_t total = delegate_.GetTotalSize();
  int deferred_drill_down = -1;
  bool deferred_navigate_parent = false;

  // Use actual (non-renormalized) fractions for bar widths.
  std::vector<float> real_fractions;
  real_fractions.reserve(slices.size());
  for (auto& s : slices) {
    real_fractions.push_back(static_cast<float>(s.size) /
                             static_cast<float>(total));
  }
  float max_fraction =
      real_fractions.empty()
          ? 1.0f
          : *std::max_element(real_fractions.begin(), real_fractions.end());
  if (max_fraction <= 0)
    max_fraction = 1.0f;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float bar_height = ImGui::GetTextLineHeightWithSpacing();
  float bar_gap = 2.0f;
  float label_margin = 8.0f;

  if (ImGui::BeginChild("##bar_chart", ImVec2(-FLT_MIN, -FLT_MIN))) {
    auto* dl = ImGui::GetWindowDrawList();
    float max_bar_width = avail.x * 0.55f;

    for (int si = 0; si < static_cast<int>(slices.size()); si++) {
      auto& slice = slices[si];
      float bar_w = (real_fractions[si] / max_fraction) * max_bar_width;
      bar_w = std::max(bar_w, 4.0f);

      ImVec2 cursor = ImGui::GetCursorScreenPos();
      ImVec2 bar_min = cursor;
      ImVec2 bar_max(cursor.x + bar_w, cursor.y + bar_height - bar_gap);

      // Full-width invisible button for hover/click detection.
      ImGui::PushID(si);
      ImGui::InvisibleButton("##bar", ImVec2(avail.x, bar_height));
      bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();

      bool is_others = (slice.child_index < 0);
      ImU32 col = is_others ? (hovered ? IM_COL32(0x99, 0x99, 0x99, 0xFF)
                                       : IM_COL32(0x77, 0x77, 0x77, 0xFF))
                            : (hovered ? SliceColorLight(si) : SliceColor(si));

      float rounding = (bar_height - bar_gap) * 0.15f;
      dl->AddRectFilled(bar_min, bar_max, col, rounding);

      if (hovered) {
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        bool light_bg = (bg.x * 0.299f + bg.y * 0.587f + bg.z * 0.114f) > 0.5f;
        ImU32 border_col =
            light_bg ? IM_COL32(0, 0, 0, 200) : IM_COL32(255, 255, 255, 200);
        dl->AddRect(bar_min, bar_max, border_col, rounding, 0, 1.5f);
      }

      // Label to the right of the bar.
      char label[256];
      float real_pct = real_fractions[si] * 100.0f;
      if (slice.child_index >= 0) {
        int ci = slice.child_index;
        snprintf(label, sizeof(label), "%s%s  %s  (%.1f%%)",
                 delegate_.GetChildName(ci).c_str(),
                 delegate_.IsChildDirectory(ci) ? "/" : "",
                 HumanSize(delegate_.GetChildSize(ci)).c_str(), real_pct);
      } else {
        snprintf(label, sizeof(label), "Others (%d items)  %s  (%.1f%%)",
                 slice.others_count, HumanSize(slice.size).c_str(), real_pct);
      }
      ImVec2 text_pos(
          bar_max.x + label_margin,
          bar_min.y +
              (bar_height - bar_gap - ImGui::GetTextLineHeight()) * 0.5f);
      dl->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);

      // Tooltip.
      if (hovered) {
        if (slice.child_index >= 0) {
          int ci = slice.child_index;
          ImGui::SetTooltip(
              "%s%s\n%s (%.1f%%)", delegate_.GetChildName(ci).c_str(),
              delegate_.IsChildDirectory(ci) ? "/" : "",
              HumanSize(delegate_.GetChildSize(ci)).c_str(), real_pct);
          if (delegate_.IsChildDirectory(ci) &&
              ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            deferred_drill_down = ci;
          }
        } else {
          ImGui::SetTooltip("Others (%d items)\n%s (%.1f%%)",
                            slice.others_count, HumanSize(slice.size).c_str(),
                            real_pct);
        }
      }
    }

    // Right-click to go to parent.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      deferred_navigate_parent = true;
    }
  }
  ImGui::EndChild();

  // Execute deferred navigation after all rendering is complete, since
  // DrillDown/NavigateToParent change the delegate's state and would
  // invalidate child indices used by subsequent loop iterations above.
  if (deferred_drill_down >= 0)
    delegate_.DrillDown(deferred_drill_down);
  else if (deferred_navigate_parent)
    delegate_.NavigateToParent();
}
