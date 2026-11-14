// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_CHANGE_SEARCH_H
#define GEL_COMMANDS_GIT_CHANGE_SEARCH_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/double_buffer.h"
#include "gel/commands/git.h"
#include "gel/commands/git_log.h"

// Runs "git diff-tree --stdin" over a list of commits to find the ones whose
// changes match, for the commit search modes that look at what a commit
// changed rather than at its metadata: pickaxe (-S / -G) and touched paths.
//
// The commits are fed in order with a marker line before every
// kMarkerInterval of them. diff-tree echoes lines that are not commits, so the
// markers come back in order with the matches: they report progress and tell
// which part of the list each matching commit came from.
class GitChangeSearch final : public Git {
 public:
  // What to match. An empty query matches nothing and must not be run.
  struct Query {
    std::vector<std::string> git_options;   // Before "diff-tree".
    std::vector<std::string> diff_options;  // e.g. "-Sfoo", "-i".
    std::vector<std::string> pathspecs;

    bool empty() const { return diff_options.empty() && pathspecs.empty(); }
    bool operator==(const Query& other) const = default;
  };

  // Positions are indices into the list of commits passed to Run().
  struct Event {
    enum class Kind : uint8_t {
      // Every commit before |position| has been checked.
      kProgress,
      // |text| is a matching commit. Its position is at or after |position|
      // and less than |position| + kMarkerInterval.
      kMatch,
      // The run ended. |failed| is set if git failed, with its error output
      // in |text|.
      kFinished,
    };
    int run_id = 0;
    Kind kind = Kind::kProgress;
    bool failed = false;
    int position = 0;
    std::string text;
  };

  static constexpr int kMarkerInterval = 64;

  explicit GitChangeSearch(std::function<void(bool)> busy_callback);
  ~GitChangeSearch() final;

  GitChangeSearch(GitChangeSearch const&) = delete;
  GitChangeSearch& operator=(GitChangeSearch const&) = delete;

  // Checks the commits at |indices| in |commits| against |query|, in that
  // order. Events of this run carry |run_id|. Any previous run is killed.
  bool Run(int run_id,
           const Query& query,
           std::span<const GitLog::CommitInfo> commits,
           std::span<const int> indices);

  void Update() { did_clear_ = events_.Merge(); }

  // True if a new run started since the previous Update(), in which case
  // GetEvents() starts over with the events of the new run.
  bool DidClear() const { return did_clear_; }

  std::span<const Event> GetEvents() const { return events_.data(); }

 private:
  DoubleBuffer<Event> events_;
  bool did_clear_ = false;

  // Worker thread.
  int marker_position_ = 0;
  std::string err_;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnError(std::string line) final;
  void OnFinished(Exec::Status status, int result) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_CHANGE_SEARCH_H
