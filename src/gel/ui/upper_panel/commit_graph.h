// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H
#define GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "gel/commands/git_log.h"
#include "third_party/imgui/imgui/imgui.h"

struct GraphRow {
  int commit_col = -1;
  int max_col = 0;
  ImU32 commit_color = 0;
  ImU32 reconnect_arrow_color = 0;

  struct Edge {
    int from_col;
    int to_col;
    ImU32 color;
    bool arrow = false;
  };

  std::vector<Edge> upper_edges;  // Row top -> row center.
  std::vector<Edge> lower_edges;  // Row center -> row bottom.
};

class CommitGraph {
 public:
  void Update(std::span<const GitLog::CommitInfo> commits);
  void Reset();
  void Draw(ImDrawList* dl,
            int commit_index,
            ImVec2 row_origin,
            float row_height,
            float lane_spacing) const;
  int max_columns() const { return max_columns_; }
  int RowColumns(int commit_index) const;

 private:
  struct Lane {
    std::string hash;
    int color_index;
    int start_row = 0;
  };

  std::vector<Lane> lanes_;
  std::vector<GraphRow> rows_;
  std::unordered_map<std::string, int> terminated_;  // hash -> color_index.
  size_t processed_count_ = 0;
  int max_columns_ = 0;
  int next_color_ = 0;

  // Reusable buffers to avoid per-iteration allocations.
  std::vector<int> matching_;
  std::vector<Lane> new_branch_lanes_;
  std::vector<int> fork_lanes_;
  std::vector<std::pair<int, ImU32>> stale_lanes_;
  std::vector<int> old_to_new_;
  std::vector<Lane> arranged_;
};

#endif  // GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H
