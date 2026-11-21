// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H
#define GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "gel/commands/git_log.h"
#include "third_party/imgui/imgui/imgui.h"

// Lays out the commit graph: the lane each commit sits in and the edges
// joining the lanes from one row to the next. The layout is built
// incrementally as commits stream in. It takes a few bytes for every commit in
// the log, so it's kept compact: 8 bytes a row and 2 an edge.
class CommitGraph {
 public:
  // The most lanes the layout supports. Up to 8 fit in the rows and edges.
  static constexpr int kMaxLanes = 5;

  CommitGraph();
  ~CommitGraph();

  CommitGraph(const CommitGraph&) = delete;
  CommitGraph& operator=(const CommitGraph&) = delete;

  // Lays out the commits past the ones already laid out. |commits| must start
  // with the ones laid out before, until Reset().
  void Update(std::span<const GitLog::CommitInfo> commits);
  void Reset();
  // Caps the lanes at |max_lanes| (1 to kMaxLanes). Lanes past it are cut with
  // a stub arrow and reconnect when their commit shows up. Changing it
  // rebuilds the layout on the next Update().
  void SetMaxLanes(int max_lanes);
  void Draw(ImDrawList* dl,
            int commit_index,
            ImVec2 row_origin,
            float row_height,
            float lane_spacing) const;
  // The columns the row of |commit_index| spans, or 0 if it isn't laid out.
  int RowColumns(int commit_index) const;

 private:
  // A line between lane columns, from the center of a row to its bottom.
  struct Edge {
    uint16_t from_col : 3;
    uint16_t to_col : 3;
    uint16_t color : 3;  // Palette index.
    // A stub ending in an arrow, where a lane is cut.
    uint16_t arrow : 1;
    // A stub pointing off to the right of |to_col|, into the gap between the
    // lanes, rather than down at it.
    uint16_t side : 1;
  };

  enum class EdgeKind { kLine, kStub, kSideStub };

  // The edges from the top of a row to its center aren't stored: they are the
  // lanes leaving the row above through its edges, but for its stubs, and go
  // straight down but for the ones merging into the commit.
  struct Row {
    // Index of the row's first edge in |edges_|.
    uint32_t first_edge;
    uint8_t edge_count;
    // The columns of the lanes merging into the commit from above.
    uint8_t merge_mask;
    uint16_t commit_col : 3;
    uint16_t commit_color : 3;
    // Palette index + 1 of an arrow above the commit's dot, where a cut lane
    // reconnects. 0 for none.
    uint16_t reconnect_color : 4;
    uint16_t max_col : 3;
  };

  static_assert(sizeof(Edge) == 2);
  static_assert(sizeof(Row) == 8);

  // An array grown in fixed-size chunks: nothing is moved as it grows, and
  // the slack is at most one chunk, where a vector would double. Clearing
  // frees the chunks, so a large log's don't outlive it.
  template <typename T>
  class ChunkedArray {
   public:
    size_t size() const { return size_; }
    T& operator[](size_t i) { return chunks_[i >> kShift][i & kMask]; }
    const T& operator[](size_t i) const {
      return chunks_[i >> kShift][i & kMask];
    }
    void push_back(const T& value) {
      if ((size_ >> kShift) == chunks_.size())
        chunks_.emplace_back(new T[kChunkSize]);
      (*this)[size_++] = value;
    }
    void clear() {
      chunks_.clear();
      size_ = 0;
    }

   private:
    static constexpr unsigned kShift = 14;
    static constexpr size_t kChunkSize = size_t{1} << kShift;
    static constexpr size_t kMask = kChunkSize - 1;

    std::vector<std::unique_ptr<T[]>> chunks_;
    size_t size_ = 0;
  };

  // Fits a SHA-256 commit hash in hex.
  static constexpr size_t kMaxHashSize = 64;

  // A commit hash kept inline, saving an allocation for each.
  struct Hash {
    std::string_view view() const { return {data, size}; }
    void Set(std::string_view h);

    char data[kMaxHashSize];
    uint8_t size;
  };

  // A lane, waiting for the commit with |hash|.
  struct Lane {
    Hash hash;
    uint8_t color;  // Palette index.
    // The row the lane started in or last had a commit in.
    int start_row;
  };

  // The lanes cut before their commit showed up: the commit's hash -> the
  // lane's palette index. About every other row cuts a lane, so this is a flat
  // open-addressing table that doesn't allocate for each. An entry stays until
  // its commit shows up, which for a parent outside a revision range is never,
  // so each takes a slot of 8 bytes: a 61-bit fingerprint of the hash and the
  // palette index. Two hashes sharing a fingerprint, at odds of 1 in 2^61,
  // would only draw a reconnect arrow where there is none.
  class CutLanes {
   public:
    bool empty() const { return count_ == 0; }
    void Insert(std::string_view hash, uint8_t color);
    // Removes |hash| and returns its palette index, or -1 if it isn't there.
    int Take(std::string_view hash);
    // Removes all and frees the table.
    void Clear();

   private:
    static constexpr unsigned kColorBits = 3;

    // A nonzero fingerprint of |hash|, in the bits a slot has for it.
    static uint64_t Fingerprint(std::string_view hash);
    // The slot holding |fingerprint|, or the empty one it would go in.
    size_t Find(uint64_t fingerprint) const;
    void Grow();

    // Fingerprint << kColorBits | palette index, or 0 for an empty slot. A
    // power of 2 of them, or none.
    std::vector<uint64_t> slots_;
    size_t count_ = 0;
  };

  // A lane for a merge's parent that no lane tracks yet.
  struct NewBranch {
    size_t parent;  // Index in the commit's parents.
    uint8_t color;
  };

  int NextColor();
  // Ends |lane|, letting its commit reconnect when it shows up.
  void Terminate(const Lane& lane) {
    cut_lanes_.Insert(lane.hash.view(), lane.color);
  }
  void AddEdge(int from_col,
               int to_col,
               int color,
               EdgeKind kind = EdgeKind::kLine);

  ChunkedArray<Row> rows_;
  ChunkedArray<Edge> edges_;
  std::array<Lane, kMaxLanes> lanes_;
  int lane_count_ = 0;
  CutLanes cut_lanes_;
  int max_lanes_ = kMaxLanes;
  int next_color_ = 0;

  // Per-row scratch, kept to avoid allocating for every row.
  std::vector<NewBranch> new_branches_;
};

#endif  // GEL_UI_UPPER_PANEL_COMMIT_GRAPH_H
