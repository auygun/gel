// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_DIFF_TREE_H
#define GEL_COMMANDS_GIT_DIFF_TREE_H

#include <mutex>
#include <string>
#include <vector>

#include "gel/commands/git.h"

// A file changed by a commit, with its new blob hash.
struct DiffTreeEntry {
  std::string path;
  std::string new_hash;
  bool deleted = false;
};

// Runs "git diff-tree" to list files changed by a commit with their blob
// hashes. Results are delivered to the main thread via Update().
class GitDiffTree final : public Git {
 public:
  explicit GitDiffTree(std::function<void(bool)> busy_callback);
  ~GitDiffTree() final;

  GitDiffTree(GitDiffTree const&) = delete;
  GitDiffTree& operator=(GitDiffTree const&) = delete;

  bool Run(const std::string& commit_hash);

  // Main thread: returns true if new results are available since the last
  // call, filling |result| with the entry list.
  bool Update(std::vector<DiffTreeEntry>& result);

 private:
  // Worker-thread state for accumulating diff-tree output.
  std::vector<DiffTreeEntry> entries_;

  // Result passed from worker to main thread.
  std::mutex result_lock_;
  bool has_result_ = false;
  std::vector<DiffTreeEntry> result_;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final;
};

#endif  // GEL_COMMANDS_GIT_DIFF_TREE_H
