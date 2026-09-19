// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/upper_panel/commit_graph.h"

#include <algorithm>

namespace {

constexpr ImU32 kPalette[] = {
    IM_COL32(0x00, 0x9E, 0x73, 0xFF),  // Teal.
    IM_COL32(0xE6, 0x9F, 0x00, 0xFF),  // Orange.
    IM_COL32(0x56, 0xB4, 0xE9, 0xFF),  // Sky blue.
    IM_COL32(0xF0, 0xE4, 0x42, 0xFF),  // Yellow.
    IM_COL32(0xD5, 0x5E, 0x00, 0xFF),  // Vermillion.
    IM_COL32(0xCC, 0x79, 0xA7, 0xFF),  // Pink.
    IM_COL32(0x00, 0x72, 0xB2, 0xFF),  // Blue.
    IM_COL32(0x99, 0x99, 0x99, 0xFF),  // Grey.
};
constexpr int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);
constexpr int kMaxIdleRows = 4;

}  // namespace

void CommitGraph::Reset() {
  for (auto& lane : lanes_)
    lane.hash.clear();
  terminated_.clear();
  processed_count_ = 0;
  max_columns_ = 0;
  next_color_ = 0;
}

void CommitGraph::Update(std::span<const GitLog::CommitInfo> commits) {
  for (size_t i = processed_count_; i < commits.size(); i++) {
    const auto& ci = commits[i];
    if (i >= rows_.size())
      rows_.emplace_back();
    auto& row = rows_[i];
    row.upper_edges.clear();
    row.lower_edges.clear();
    row.reconnect_arrow_color = 0;

    // Find all lanes waiting for this commit.
    int primary = -1;
    matching_.clear();
    for (int l = 0; l < (int)lanes_.size(); l++) {
      if (lanes_[l].hash == ci.commit) {
        if (primary < 0)
          primary = l;
        matching_.push_back(l);
      }
    }

    // No lane is waiting — new branch head. Find leftmost free slot or append.
    bool is_new_head = primary < 0;
    if (is_new_head) {
      // Check if this commit was previously tracked by a terminated lane.
      int reconnect_color = -1;
      auto term_it = terminated_.find(ci.commit);
      if (term_it != terminated_.end()) {
        reconnect_color = term_it->second;
        row.reconnect_arrow_color = kPalette[reconnect_color];
        terminated_.erase(term_it);
      }

      int color = (reconnect_color >= 0) ? reconnect_color
                                         : next_color_++ % kPaletteSize;
      int slot = -1;
      for (int l = 0; l < (int)lanes_.size(); l++) {
        if (lanes_[l].hash.empty()) {
          slot = l;
          break;
        }
      }
      if (slot >= 0) {
        primary = slot;
        lanes_[slot] = {ci.commit, color, (int)i};
      } else {
        primary = (int)lanes_.size();
        lanes_.push_back({ci.commit, color, (int)i});
      }
      matching_.push_back(primary);
    } else {
      // Commit matched an active lane but may also have had a terminated
      // lane waiting for it. Draw a reconnect arrow to indicate there's
      // an additional connection from somewhere above that was cut short.
      auto term_it = terminated_.find(ci.commit);
      if (term_it != terminated_.end()) {
        row.reconnect_arrow_color = kPalette[term_it->second];
        terminated_.erase(term_it);
      }
    }

    row.commit_col = primary;
    row.commit_color = kPalette[lanes_[primary].color_index];

    // Upper edges: all active lanes draw a line from row top to center.
    // Non-primary matching lanes merge diagonally to primary.
    // Skip primary for new heads (nothing above it).
    for (int l = 0; l < (int)lanes_.size(); l++) {
      if (lanes_[l].hash.empty())
        continue;
      if (l == primary && is_new_head)
        continue;
      bool is_match =
          std::find(matching_.begin(), matching_.end(), l) != matching_.end();
      if (is_match && l != primary) {
        row.upper_edges.push_back(
            {l, primary, kPalette[lanes_[l].color_index]});
      } else {
        row.upper_edges.push_back({l, l, kPalette[lanes_[l].color_index]});
      }
    }

    // Collapse non-primary matching lanes (merge complete).
    for (int l : matching_) {
      if (l != primary)
        lanes_[l].hash.clear();
    }

    // Primary lane continues with first parent, or freed for root commits.
    if (!ci.parents.empty()) {
      lanes_[primary].hash = ci.parents[0];
      lanes_[primary].start_row = (int)i;
    } else {
      lanes_[primary].hash.clear();
    }

    // Collect new branch lanes and fork lanes for additional parents.
    new_branch_lanes_.clear();
    fork_lanes_.clear();
    for (size_t p = 1; p < ci.parents.size(); p++) {
      const auto& parent = ci.parents[p];
      // Check if any existing lane already tracks this parent.
      int existing = -1;
      for (int l = 0; l < (int)lanes_.size(); l++) {
        if (lanes_[l].hash == parent) {
          existing = l;
          break;
        }
      }
      if (existing >= 0) {
        fork_lanes_.push_back(existing);
      } else {
        new_branch_lanes_.push_back(
            {parent, next_color_++ % kPaletteSize, (int)i});
      }
    }

    // Terminate stale lanes: idle for more than kMaxIdleRows rows.
    // Save hash and color so the commit can reconnect later.
    stale_lanes_.clear();
    for (int l = 0; l < (int)lanes_.size(); l++) {
      if (!lanes_[l].hash.empty() &&
          (int)i - lanes_[l].start_row > kMaxIdleRows) {
        stale_lanes_.emplace_back(l, kPalette[lanes_[l].color_index]);
        terminated_[lanes_[l].hash] = lanes_[l].color_index;
        lanes_[l].hash.clear();
      }
    }

    // Lane compaction: rearrange as [0..primary] + [new branches] +
    // [primary+1..end], removing empty lane slots.
    int old_lane_count = (int)lanes_.size();
    old_to_new_.assign(old_lane_count, -1);
    arranged_.clear();

    for (int l = 0; l <= primary && l < old_lane_count; l++) {
      if (!lanes_[l].hash.empty()) {
        old_to_new_[l] = (int)arranged_.size();
        arranged_.push_back(std::move(lanes_[l]));
      }
    }

    int first_new_col = (int)arranged_.size();
    for (auto& nb : new_branch_lanes_)
      arranged_.push_back(std::move(nb));

    for (int l = primary + 1; l < old_lane_count; l++) {
      if (!lanes_[l].hash.empty()) {
        old_to_new_[l] = (int)arranged_.size();
        arranged_.push_back(std::move(lanes_[l]));
      }
    }
    std::swap(lanes_, arranged_);

    // Lower edges: existing lanes map from old position to new position.
    for (int old_col = 0; old_col < old_lane_count; old_col++) {
      int new_col = old_to_new_[old_col];
      if (new_col < 0)
        continue;
      row.lower_edges.push_back(
          {old_col, new_col, kPalette[lanes_[new_col].color_index]});
    }

    // Lower edges for new branch lanes (fork from primary column).
    for (int b = 0; b < (int)new_branch_lanes_.size(); b++) {
      int new_col = first_new_col + b;
      row.lower_edges.push_back(
          {primary, new_col, kPalette[lanes_[new_col].color_index]});
    }

    // Fork edges for already-tracked parent lanes.
    for (int old_col : fork_lanes_) {
      if (old_col < old_lane_count) {
        int new_col = old_to_new_[old_col];
        if (new_col >= 0) {
          row.lower_edges.push_back(
              {primary, new_col, kPalette[lanes_[new_col].color_index]});
        }
      }
    }

    // Stub arrow edges for terminated stale lanes.
    for (auto& [old_col, color] : stale_lanes_)
      row.lower_edges.push_back({old_col, old_col, color, true});

    // Track max column used across all edges and the commit dot.
    int max_col = row.commit_col;
    for (auto& e : row.upper_edges)
      max_col = std::max(max_col, std::max(e.from_col, e.to_col));
    for (auto& e : row.lower_edges)
      max_col = std::max(max_col, std::max(e.from_col, e.to_col));
    row.max_col = max_col;
    max_columns_ = std::max(max_columns_, max_col + 1);
  }

  processed_count_ = commits.size();
}

int CommitGraph::RowColumns(int commit_index) const {
  if (commit_index < 0 || commit_index >= (int)processed_count_)
    return 0;
  return rows_[commit_index].max_col + 1;
}

void CommitGraph::Draw(ImDrawList* dl,
                       int commit_index,
                       ImVec2 row_origin,
                       float row_height,
                       float lane_spacing) const {
  if (commit_index < 0 || commit_index >= (int)processed_count_)
    return;

  const auto& row = rows_[commit_index];
  float half = row_height * 0.5f;
  float line_width = 2.0f;
  float dot_radius = lane_spacing * 0.3f;

  auto col_x = [&](int col) -> float {
    return row_origin.x + lane_spacing * 0.5f + col * lane_spacing;
  };

  // Draw upper edges (top of row -> center).
  for (const auto& e : row.upper_edges) {
    ImVec2 p0(col_x(e.from_col), row_origin.y);
    ImVec2 p1(col_x(e.to_col), row_origin.y + half);
    dl->AddLine(p0, p1, e.color, line_width);
  }

  // Draw lower edges (center -> bottom of row).
  for (const auto& e : row.lower_edges) {
    ImVec2 p0(col_x(e.from_col), row_origin.y + half);
    if (e.arrow) {
      // Stub arrow for terminated stale lane.
      float ah = dot_radius * 1.5f;
      float aw = dot_radius;
      float gap = half * 0.8f;
      float arrow_tip_y = row_origin.y + row_height - gap;
      float arrow_base_y = arrow_tip_y - ah;
      float x = col_x(e.to_col);
      dl->AddLine(p0, ImVec2(x, arrow_base_y), e.color, line_width);
      dl->AddTriangleFilled(ImVec2(x - aw, arrow_base_y),
                            ImVec2(x + aw, arrow_base_y),
                            ImVec2(x, arrow_tip_y), e.color);
    } else {
      ImVec2 p1(col_x(e.to_col), row_origin.y + row_height);
      dl->AddLine(p0, p1, e.color, line_width);
    }
  }

  // Draw commit dot.
  if (row.commit_col >= 0) {
    ImVec2 center(col_x(row.commit_col), row_origin.y + half);
    dl->AddCircleFilled(center, dot_radius, row.commit_color);

    // Reconnect arrow above the dot for commits resuming a terminated lane.
    if (row.reconnect_arrow_color) {
      float ah = dot_radius * 1.5f;
      float aw = dot_radius;
      float arrow_bottom = center.y - dot_radius - 1.0f;
      float arrow_top = arrow_bottom - ah;
      dl->AddTriangleFilled(ImVec2(center.x - aw, arrow_bottom),
                            ImVec2(center.x + aw, arrow_bottom),
                            ImVec2(center.x, arrow_top),
                            row.reconnect_arrow_color);
    }
  }
}
