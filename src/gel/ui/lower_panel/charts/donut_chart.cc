// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/charts/donut_chart.h"

#include <algorithm>
#include <cmath>

#include "third_party/imgui/imgui/imgui.h"

namespace {
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

void DonutChart::Render(const std::vector<SliceInfo>& slices) {
  int64_t total = delegate_.GetTotalSize();
  int deferred_drill_down = -1;
  bool deferred_navigate_parent = false;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float chart_size = std::min(avail.x * 0.5f, avail.y - 10.0f);
  chart_size = std::max(chart_size, 80.0f);
  float outer_radius = chart_size * 0.45f;
  float inner_radius = outer_radius * 0.55f;
  float gap_angle = 0.012f;

  float vertical_pad = std::max(0.0f, (avail.y - chart_size) * 0.5f);
  ImVec2 chart_top_left = ImGui::GetCursorScreenPos();
  chart_top_left.y += vertical_pad;
  ImVec2 center(chart_top_left.x + chart_size * 0.5f,
                chart_top_left.y + chart_size * 0.5f);

  auto* dl = ImGui::GetWindowDrawList();
  ImVec2 mouse = ImGui::GetMousePos();

  int hovered_slice = -1;

  bool single_slice = (slices.size() == 1);
  float max_total_gap = 2.0f * kPi * 0.1f;
  float effective_gap =
      single_slice ? 0.0f
                   : std::min(gap_angle, max_total_gap /
                                             static_cast<float>(slices.size()));

  float dx = mouse.x - center.x;
  float dy = mouse.y - center.y;
  float dist = sqrtf(dx * dx + dy * dy);
  float mouse_angle_val = atan2f(dy, dx);

  float angle_start = -kPi * 0.5f;
  for (size_t si = 0; si < slices.size(); si++) {
    float sweep = slices[si].fraction * 2.0f * kPi;
    float a0 = angle_start + effective_gap * 0.5f;
    float a1 = angle_start + sweep - effective_gap * 0.5f;
    if (a1 > a0 && dist >= inner_radius && dist <= outer_radius) {
      auto normalize = [](float a) {
        while (a < -kPi)
          a += 2.0f * kPi;
        while (a > kPi)
          a -= 2.0f * kPi;
        return a;
      };
      float ma = normalize(mouse_angle_val);
      float sa = normalize(a0);
      float ea = normalize(a1);
      bool in_slice =
          (sa <= ea) ? (ma >= sa && ma < ea) : (ma >= sa || ma < ea);
      if (in_slice)
        hovered_slice = static_cast<int>(si);
    }
    angle_start += sweep;
  }

  int gradient_bands = 3;
  angle_start = -kPi * 0.5f;
  for (size_t si = 0; si < slices.size(); si++) {
    float sweep = slices[si].fraction * 2.0f * kPi;
    float a0 = angle_start + effective_gap * 0.5f;
    float a1 = angle_start + sweep - effective_gap * 0.5f;

    if (a1 <= a0) {
      angle_start += sweep;
      continue;
    }

    bool hovered = (hovered_slice == static_cast<int>(si));
    int idx = static_cast<int>(si);
    float cur_outer = outer_radius;

    int num_segments = std::max(4, static_cast<int>(sweep * 40.0f));
    float step = (a1 - a0) / num_segments;

    float radial_overlap = 6.0f;
    float angular_overlap = 6.0f / cur_outer;
    for (int band = 0; band < gradient_bands; band++) {
      float t0 = static_cast<float>(band) / gradient_bands;
      float t1 = static_cast<float>(band + 1) / gradient_bands;
      float r0 = inner_radius + (cur_outer - inner_radius) * t0;
      float r1 = inner_radius + (cur_outer - inner_radius) * t1;
      float r0e = r0 - (band > 0 ? radial_overlap : 0);
      float r1e = r1 + (band < gradient_bands - 1 ? radial_overlap : 0);

      bool is_others = (slices[si].child_index < 0);
      ImU32 col_inner =
          is_others ? IM_COL32(0x55, 0x55, 0x55, 0xFF) : SliceColorDark(idx);
      ImU32 col_outer =
          is_others ? (hovered ? IM_COL32(0x99, 0x99, 0x99, 0xFF)
                               : IM_COL32(0x77, 0x77, 0x77, 0xFF))
                    : (hovered ? SliceColorLight(idx) : SliceColor(idx));
      ImU32 c0 = LerpColor(col_inner, col_outer, t0);
      ImU32 c1 = LerpColor(col_inner, col_outer, t1);
      ImU32 band_col = LerpColor(c0, c1, 0.5f);

      for (int s = 0; s < num_segments; s++) {
        float ang0 = a0 + s * step;
        float ang1 = a0 + (s + 1) * step;
        float ang0e = std::max(a0, ang0 - (s > 0 ? angular_overlap : 0));
        float ang1e =
            std::min(a1, ang1 + (s < num_segments - 1 ? angular_overlap : 0));
        float cos0 = cosf(ang0e), sin0 = sinf(ang0e);
        float cos1 = cosf(ang1e), sin1 = sinf(ang1e);

        ImVec2 p0(center.x + r0e * cos0, center.y + r0e * sin0);
        ImVec2 p1(center.x + r1e * cos0, center.y + r1e * sin0);
        ImVec2 p2(center.x + r1e * cos1, center.y + r1e * sin1);
        ImVec2 p3(center.x + r0e * cos1, center.y + r0e * sin1);
        dl->AddTriangleFilled(p0, p1, p2, band_col);
        dl->AddTriangleFilled(p0, p2, p3, band_col);
      }
    }

    if (hovered) {
      ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
      bool light_bg = (bg.x * 0.299f + bg.y * 0.587f + bg.z * 0.114f) > 0.5f;
      ImU32 border_col =
          light_bg ? IM_COL32(0, 0, 0, 200) : IM_COL32(255, 255, 255, 200);
      float thickness = 2.0f;
      for (int s = 0; s < num_segments; s++) {
        float ang0 = a0 + s * step;
        float ang1 = a0 + (s + 1) * step;
        dl->AddLine(ImVec2(center.x + cur_outer * cosf(ang0),
                           center.y + cur_outer * sinf(ang0)),
                    ImVec2(center.x + cur_outer * cosf(ang1),
                           center.y + cur_outer * sinf(ang1)),
                    border_col, thickness);
      }
      for (int s = 0; s < num_segments; s++) {
        float ang0 = a0 + s * step;
        float ang1 = a0 + (s + 1) * step;
        dl->AddLine(ImVec2(center.x + inner_radius * cosf(ang0),
                           center.y + inner_radius * sinf(ang0)),
                    ImVec2(center.x + inner_radius * cosf(ang1),
                           center.y + inner_radius * sinf(ang1)),
                    border_col, thickness);
      }
      dl->AddLine(ImVec2(center.x + inner_radius * cosf(a0),
                         center.y + inner_radius * sinf(a0)),
                  ImVec2(center.x + cur_outer * cosf(a0),
                         center.y + cur_outer * sinf(a0)),
                  border_col, thickness);
      dl->AddLine(ImVec2(center.x + inner_radius * cosf(a1),
                         center.y + inner_radius * sinf(a1)),
                  ImVec2(center.x + cur_outer * cosf(a1),
                         center.y + cur_outer * sinf(a1)),
                  border_col, thickness);
    }

    float pct = slices[si].fraction * 100.0f;
    if (pct >= 5.0f && chart_size >= 200.0f) {
      float mid_angle = (a0 + a1) * 0.5f;
      float label_r = (inner_radius + cur_outer) * 0.5f;
      ImVec2 label_pos(center.x + label_r * cosf(mid_angle),
                       center.y + label_r * sinf(mid_angle));
      char pct_buf[16];
      snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", pct);
      ImVec2 text_size = ImGui::CalcTextSize(pct_buf);
      dl->AddText(ImVec2(label_pos.x - text_size.x * 0.5f,
                         label_pos.y - text_size.y * 0.5f),
                  IM_COL32(255, 255, 255, 220), pct_buf);
    }

    angle_start += sweep;
  }

  if (chart_size >= 200.0f) {
    std::string size_str = HumanSize(total);
    ImVec2 text_size = ImGui::CalcTextSize(size_str.c_str());

    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "%d items",
             delegate_.GetChildCount());
    ImVec2 count_size = ImGui::CalcTextSize(count_buf);

    float gap = 2.0f;
    float total_h = text_size.y + gap + count_size.y;
    float top_y = center.y - total_h * 0.5f;

    dl->AddText(ImVec2(center.x - text_size.x * 0.5f, top_y),
                ImGui::GetColorU32(ImGuiCol_Text), size_str.c_str());

    ImU32 dim_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    dl->AddText(
        ImVec2(center.x - count_size.x * 0.5f, top_y + text_size.y + gap),
        dim_col, count_buf);
  }

  if (hovered_slice >= 0) {
    auto& slice = slices[hovered_slice];
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

  if (dist <= outer_radius && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    deferred_navigate_parent = true;
  }

  ImGui::SetCursorScreenPos(
      ImVec2(chart_top_left.x, chart_top_left.y - vertical_pad));
  ImGui::Dummy(ImVec2(chart_size, chart_size + vertical_pad * 2.0f));

  ImGui::SameLine();
  float legend_height = chart_size + vertical_pad * 2.0f;
  if (ImGui::BeginChild("##legend", ImVec2(-FLT_MIN, legend_height))) {
    float line_h = ImGui::GetTextLineHeightWithSpacing();
    float content_h = static_cast<float>(slices.size()) * line_h;
    if (content_h < legend_height) {
      float pad = (legend_height - content_h) * 0.5f;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
    }
    auto* legend_dl = ImGui::GetWindowDrawList();
    ImGuiListClipper legend_clipper;
    legend_clipper.Begin(static_cast<int>(slices.size()));
    if (hovered_slice >= 0)
      legend_clipper.IncludeItemByIndex(hovered_slice);
    while (legend_clipper.Step()) {
      for (int si = legend_clipper.DisplayStart; si < legend_clipper.DisplayEnd;
           si++) {
        auto& slice = slices[si];
        ImU32 col = (slice.child_index >= 0) ? SliceColor(si)
                                             : IM_COL32(0x77, 0x77, 0x77, 0xFF);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float sq = ImGui::GetTextLineHeight() * 0.7f;
        float rounding = sq * 0.3f;
        legend_dl->AddRectFilled(pos, ImVec2(pos.x + sq, pos.y + sq), col,
                                 rounding);
        ImGui::Dummy(ImVec2(sq, sq));
        ImGui::SameLine();

        bool is_hovered = (hovered_slice == si);
        char label[256];
        float real_pct =
            100.0f * static_cast<float>(slice.size) / static_cast<float>(total);
        if (slice.child_index >= 0) {
          int ci = slice.child_index;
          snprintf(label, sizeof(label), "%s%s  %s  %.1f%%",
                   delegate_.GetChildName(ci).c_str(),
                   delegate_.IsChildDirectory(ci) ? "/" : "",
                   HumanSize(delegate_.GetChildSize(ci)).c_str(), real_pct);
        } else {
          snprintf(label, sizeof(label), "Others (%d items)  %s  %.1f%%",
                   slice.others_count, HumanSize(slice.size).c_str(), real_pct);
        }
        if (is_hovered) {
          ImVec2 text_pos = ImGui::GetCursorScreenPos();
          ImVec2 label_size = ImGui::CalcTextSize(label);
          ImU32 sel_col = ImGui::GetColorU32(ImGuiCol_Header);
          legend_dl->AddRectFilled(
              text_pos,
              ImVec2(text_pos.x + label_size.x, text_pos.y + label_size.y),
              sel_col);
        }
        ImGui::TextUnformatted(label);

        if (is_hovered)
          ImGui::SetScrollHereY();
      }
    }
  }
  ImGui::EndChild();

  // Execute deferred navigation after all rendering is complete, since
  // DrillDown/NavigateToParent change the delegate's state and would
  // invalidate child indices used by the legend above.
  if (deferred_drill_down >= 0)
    delegate_.DrillDown(deferred_drill_down);
  else if (deferred_navigate_parent)
    delegate_.NavigateToParent();
}
