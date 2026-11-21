// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/upper_panel/commit_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

// Lane bit masks: a bit for each lane column.
constexpr unsigned Bit(int col) {
  return 1u << static_cast<unsigned>(col);
}
constexpr bool Has(unsigned mask, int col) {
  return (mask & Bit(col)) != 0;
}

}  // namespace

// The rows, edges and cut lanes keep palette indices and lane columns in 3
// bits.
static_assert(kPaletteSize <= 8);
static_assert(CommitGraph::kMaxLanes <= 8);

CommitGraph::CommitGraph() = default;

CommitGraph::~CommitGraph() = default;

void CommitGraph::Hash::Set(std::string_view h) {
  size = static_cast<uint8_t>(std::min(h.size(), kMaxHashSize));
  memcpy(data, h.data(), size);
}

void CommitGraph::CutLanes::Insert(std::string_view hash, uint8_t color) {
  // Keep the table at most half full, for short probes.
  if ((count_ + 1) * 2 > slots_.size())
    Grow();
  uint64_t fingerprint = Fingerprint(hash);
  uint64_t& slot = slots_[Find(fingerprint)];
  if (!slot)
    count_++;
  slot = fingerprint << kColorBits | color;
}

int CommitGraph::CutLanes::Take(std::string_view hash) {
  if (count_ == 0)
    return -1;
  size_t i = Find(Fingerprint(hash));
  if (!slots_[i])
    return -1;
  int color = static_cast<int>(slots_[i] & ((1u << kColorBits) - 1));

  // Close the gap: move each later slot of the probe run into it, unless the
  // slot's home lies cyclically between the gap and the slot.
  size_t mask = slots_.size() - 1;
  for (size_t j = (i + 1) & mask; slots_[j]; j = (j + 1) & mask) {
    size_t home = static_cast<size_t>(slots_[j] >> kColorBits) & mask;
    bool stays = i < j ? (i < home && home <= j) : (i < home || home <= j);
    if (!stays) {
      slots_[i] = slots_[j];
      i = j;
    }
  }
  slots_[i] = 0;
  count_--;
  return color;
}

void CommitGraph::CutLanes::Clear() {
  slots_ = std::vector<uint64_t>();
  count_ = 0;
}

uint64_t CommitGraph::CutLanes::Fingerprint(std::string_view hash) {
  // Fold the hash in 8 bytes at a time, then mix so that every bit of the
  // result, the low ones picking the slot included, depends on all of them.
  uint64_t h = hash.size();
  size_t i = 0;
  for (; i + sizeof(uint64_t) <= hash.size(); i += sizeof(uint64_t)) {
    uint64_t word;
    memcpy(&word, hash.data() + i, sizeof(word));
    h = (h ^ word) * 0x9E3779B97F4A7C15ull;
  }
  for (; i < hash.size(); i++)
    h = (h ^ static_cast<uint8_t>(hash[i])) * 0x100000001B3ull;
  h ^= h >> 32u;
  h *= 0xBF58476D1CE4E5B9ull;
  h ^= h >> 29u;
  h >>= kColorBits;
  return h ? h : 1;
}

size_t CommitGraph::CutLanes::Find(uint64_t fingerprint) const {
  size_t mask = slots_.size() - 1;
  size_t i = static_cast<size_t>(fingerprint) & mask;
  while (slots_[i] && slots_[i] >> kColorBits != fingerprint)
    i = (i + 1) & mask;
  return i;
}

void CommitGraph::CutLanes::Grow() {
  std::vector<uint64_t> old = std::move(slots_);
  slots_.assign(std::max<size_t>(old.size() * 2, 64), 0);
  for (uint64_t slot : old) {
    if (slot)
      slots_[Find(slot >> kColorBits)] = slot;
  }
}

void CommitGraph::Reset() {
  rows_.clear();
  edges_.clear();
  lane_count_ = 0;
  cut_lanes_.Clear();
  next_color_ = 0;
}

void CommitGraph::SetMaxLanes(int max_lanes) {
  max_lanes = std::clamp(max_lanes, 1, kMaxLanes);
  if (max_lanes == max_lanes_)
    return;
  max_lanes_ = max_lanes;
  Reset();
}

int CommitGraph::NextColor() {
  int color = next_color_;
  next_color_ = (next_color_ + 1) % kPaletteSize;
  return color;
}

void CommitGraph::AddEdge(int from_col, int to_col, int color, EdgeKind kind) {
  Edge edge;
  edge.from_col = static_cast<uint16_t>(from_col);
  edge.to_col = static_cast<uint16_t>(to_col);
  edge.color = static_cast<uint16_t>(color);
  edge.arrow = kind != EdgeKind::kLine;
  edge.side = kind == EdgeKind::kSideStub;
  edges_.push_back(edge);
}

void CommitGraph::Update(std::span<const GitLog::CommitInfo> commits) {
  for (size_t i = rows_.size(); i < commits.size(); i++) {
    const GitLog::CommitInfo& ci = commits[i];
    const std::string_view commit = ci.commit;
    const int row_index = static_cast<int>(i);
    Row row = {};
    row.first_edge = static_cast<uint32_t>(edges_.size());

    // Find the lanes waiting for this commit. The first one carries it on,
    // the others merge into it.
    int primary = -1;
    unsigned matching = 0;
    for (int l = 0; l < lane_count_; l++) {
      if (lanes_[l].hash.view() == commit) {
        if (primary < 0)
          primary = l;
        matching |= Bit(l);
      }
    }

    // A commit a cut lane was waiting for gets an arrow above its dot, in the
    // lane's color, marking the connection from above that was cut short.
    int reconnect_color = -1;
    if (!cut_lanes_.empty()) {
      reconnect_color = cut_lanes_.Take(commit);
      row.reconnect_color = static_cast<uint16_t>(reconnect_color + 1);
    }

    // No lane is waiting for it: a new branch head, which gets a lane of its
    // own.
    if (primary < 0) {
      int color = reconnect_color >= 0 ? reconnect_color : NextColor();
      if (lane_count_ < max_lanes_) {
        primary = lane_count_++;
      } else {
        // All lanes are taken: cut the rightmost one to make room. It ends
        // with a stub in the previous row, where it can still be drawn, and
        // reconnects later.
        primary = max_lanes_ - 1;
        Terminate(lanes_[primary]);
      }
      // Nothing enters this column from above, so a stub in the previous row
      // ending in it, the cut lane's included, would point down at this
      // commit's dot as if leading to it. It points off to the side instead.
      if (i > 0) {
        const Row& prev = rows_[i - 1];
        for (uint32_t e = prev.first_edge;
             e < prev.first_edge + prev.edge_count; e++) {
          Edge& edge = edges_[e];
          if (edge.to_col == primary) {
            edge.arrow = 1;
            edge.side = 1;
          }
        }
      }
      lanes_[primary].hash.Set(commit);
      lanes_[primary].color = static_cast<uint8_t>(color);
      lanes_[primary].start_row = row_index;
      matching = Bit(primary);
    }

    row.commit_col = static_cast<uint16_t>(primary);
    row.commit_color = lanes_[primary].color;
    row.merge_mask = static_cast<uint8_t>(matching & ~Bit(primary));
    int max_col = lane_count_ - 1;

    // The merged lanes end here. The commit's lane continues with its first
    // parent, or ends at a root commit.
    unsigned alive = (Bit(lane_count_) - 1u) & ~unsigned{row.merge_mask};
    if (!ci.parents.empty()) {
      lanes_[primary].hash.Set(ci.parents[0]);
      lanes_[primary].start_row = row_index;
    } else {
      alive &= ~Bit(primary);
    }

    // The other parents fork off from the commit: into the lane already
    // waiting for one, or into a new lane.
    int forks[kMaxLanes];
    int fork_count = 0;
    unsigned fork_mask = 0;
    new_branches_.clear();
    for (size_t p = 1; p < ci.parents.size(); p++) {
      std::string_view parent = ci.parents[p];
      int existing = -1;
      for (int l = 0; l < lane_count_; l++) {
        if (Has(alive, l) && lanes_[l].hash.view() == parent) {
          existing = l;
          break;
        }
      }
      if (existing < 0) {
        new_branches_.push_back({p, static_cast<uint8_t>(NextColor())});
        continue;
      }
      // The lane leads to the parent from here, so it isn't idle.
      lanes_[existing].start_row = row_index;
      if (!Has(fork_mask, existing)) {
        fork_mask |= Bit(existing);
        forks[fork_count++] = existing;
      }
    }

    // Cut the lanes idle for more than kMaxIdleRows rows, with a stub arrow.
    // Their commits reconnect when they show up.
    struct Stub {
      int col;
      int color;
    };
    Stub stubs[kMaxLanes];
    int stub_count = 0;
    for (int l = 0; l < lane_count_; l++) {
      if (Has(alive, l) && row_index - lanes_[l].start_row > kMaxIdleRows) {
        stubs[stub_count++] = {l, lanes_[l].color};
        Terminate(lanes_[l]);
        alive &= ~Bit(l);
      }
    }

    // Compact the lanes as [0..primary] + [new branches] + [primary+1..end],
    // dropping the ended ones, and cut the ones past the cap. The lanes up to
    // the commit's always fit. Existing lanes end with a stub arrow, and the
    // commit gets a stub off to its side for the parents left without a lane,
    // instead of a fork into it. Either reconnects when its commit shows up.
    std::array<Lane, kMaxLanes> next_lanes;
    int old_to_new[kMaxLanes];
    int count = 0;
    int side_stub_color = -1;
    auto keep = [&](int l) {
      old_to_new[l] = -1;
      if (!Has(alive, l))
        return;
      if (count < max_lanes_) {
        old_to_new[l] = count;
        next_lanes[count++] = lanes_[l];
      } else {
        stubs[stub_count++] = {l, lanes_[l].color};
        Terminate(lanes_[l]);
        if (Has(fork_mask, l) && side_stub_color < 0)
          side_stub_color = lanes_[l].color;
      }
    };
    for (int l = 0; l <= primary; l++)
      keep(l);
    int first_new_col = count;
    int new_branch_count = 0;
    for (const NewBranch& nb : new_branches_) {
      if (count < max_lanes_) {
        Lane& lane = next_lanes[count++];
        lane.hash.Set(ci.parents[nb.parent]);
        lane.color = nb.color;
        lane.start_row = row_index;
        new_branch_count++;
      } else {
        cut_lanes_.Insert(ci.parents[nb.parent], nb.color);
        if (side_stub_color < 0)
          side_stub_color = nb.color;
      }
    }
    for (int l = primary + 1; l < lane_count_; l++)
      keep(l);

    // Edges, from the row's center to its bottom: the lanes moving to their new
    // columns, the forks from the commit and the stubs. One stub to the side
    // of the commit stands for all its parents left without a lane.
    int old_lane_count = lane_count_;
    std::copy_n(next_lanes.begin(), count, lanes_.begin());
    lane_count_ = count;
    for (int l = 0; l < old_lane_count; l++) {
      if (old_to_new[l] >= 0)
        AddEdge(l, old_to_new[l], lanes_[old_to_new[l]].color);
    }
    for (int b = 0; b < new_branch_count; b++) {
      int col = first_new_col + b;
      AddEdge(primary, col, lanes_[col].color);
    }
    for (int f = 0; f < fork_count; f++) {
      int col = old_to_new[forks[f]];
      if (col >= 0)
        AddEdge(primary, col, lanes_[col].color);
    }
    for (int s = 0; s < stub_count; s++)
      AddEdge(stubs[s].col, stubs[s].col, stubs[s].color, EdgeKind::kStub);
    if (side_stub_color >= 0)
      AddEdge(primary, primary, side_stub_color, EdgeKind::kSideStub);

    // Edges join the old lanes and the new, so the row spans the more of the
    // two.
    max_col = std::max({max_col, old_lane_count - 1, lane_count_ - 1});
    row.max_col = static_cast<uint16_t>(max_col);
    row.edge_count = static_cast<uint8_t>(edges_.size() - row.first_edge);
    rows_.push_back(row);
  }
}

int CommitGraph::RowColumns(int commit_index) const {
  if (commit_index < 0 || static_cast<size_t>(commit_index) >= rows_.size())
    return 0;
  return rows_[commit_index].max_col + 1;
}

void CommitGraph::Draw(ImDrawList* dl,
                       int commit_index,
                       ImVec2 row_origin,
                       float row_height,
                       float lane_spacing) const {
  if (commit_index < 0 || static_cast<size_t>(commit_index) >= rows_.size())
    return;

  const Row& row = rows_[commit_index];
  float half = row_height * 0.5f;
  float line_width = 2.0f;
  float dot_radius = lane_spacing * 0.3f;

  auto col_x = [&](int col) -> float {
    return row_origin.x + lane_spacing * 0.5f + col * lane_spacing;
  };

  // Top of row -> center: the lanes entering the row, which the row above
  // leaves through its edges, but for its stubs. A new head's lane has nothing
  // above it, so none of them enters the commit's column unless merging into
  // the commit.
  if (commit_index > 0) {
    const Row& prev = rows_[commit_index - 1];
    unsigned entering = 0;
    int colors[kMaxLanes] = {};
    for (uint32_t e = prev.first_edge; e < prev.first_edge + prev.edge_count;
         e++) {
      const Edge& edge = edges_[e];
      if (!edge.arrow) {
        entering |= Bit(edge.to_col);
        colors[edge.to_col] = edge.color;
      }
    }
    for (int col = 0; col < kMaxLanes; col++) {
      if (!Has(entering, col))
        continue;
      int to_col = Has(row.merge_mask, col) ? row.commit_col : col;
      dl->AddLine(ImVec2(col_x(col), row_origin.y),
                  ImVec2(col_x(to_col), row_origin.y + half),
                  kPalette[colors[col]], line_width);
    }
  }

  // Center -> bottom of row.
  for (uint32_t e = row.first_edge; e < row.first_edge + row.edge_count; e++) {
    const Edge& edge = edges_[e];
    ImU32 color = kPalette[edge.color];
    ImVec2 p0(col_x(edge.from_col), row_origin.y + half);
    if (edge.arrow && edge.side) {
      // Stub arrow pointing off to the right, into the gap between the lanes.
      float ah = dot_radius * 1.5f;
      float aw = dot_radius;
      ImVec2 tip(col_x(edge.to_col) + lane_spacing * 0.5f,
                 row_origin.y + row_height - half * 0.4f);
      float dx = tip.x - p0.x;
      float dy = tip.y - p0.y;
      float length = std::sqrt(dx * dx + dy * dy);
      dx /= length;
      dy /= length;
      ImVec2 base(tip.x - dx * ah, tip.y - dy * ah);
      dl->AddLine(p0, base, color, line_width);
      dl->AddTriangleFilled(ImVec2(base.x - dy * aw, base.y + dx * aw),
                            ImVec2(base.x + dy * aw, base.y - dx * aw), tip,
                            color);
    } else if (edge.arrow) {
      // Stub arrow for a cut lane.
      float ah = dot_radius * 1.5f;
      float aw = dot_radius;
      float gap = half * 0.8f;
      float arrow_tip_y = row_origin.y + row_height - gap;
      float arrow_base_y = arrow_tip_y - ah;
      float x = col_x(edge.to_col);
      dl->AddLine(p0, ImVec2(x, arrow_base_y), color, line_width);
      dl->AddTriangleFilled(ImVec2(x - aw, arrow_base_y),
                            ImVec2(x + aw, arrow_base_y),
                            ImVec2(x, arrow_tip_y), color);
    } else {
      ImVec2 p1(col_x(edge.to_col), row_origin.y + row_height);
      dl->AddLine(p0, p1, color, line_width);
    }
  }

  // Commit dot.
  ImVec2 center(col_x(row.commit_col), row_origin.y + half);
  dl->AddCircleFilled(center, dot_radius, kPalette[row.commit_color]);

  // Reconnect arrow above the dot for commits resuming a cut lane.
  if (row.reconnect_color) {
    float ah = dot_radius * 1.5f;
    float aw = dot_radius;
    float arrow_bottom = center.y - dot_radius - 1.0f;
    float arrow_top = arrow_bottom - ah;
    dl->AddTriangleFilled(ImVec2(center.x - aw, arrow_bottom),
                          ImVec2(center.x + aw, arrow_bottom),
                          ImVec2(center.x, arrow_top),
                          kPalette[row.reconnect_color - 1]);
  }
}
