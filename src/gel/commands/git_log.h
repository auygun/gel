// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_LOG_H
#define GEL_COMMANDS_GIT_LOG_H

#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

class GitLog final : public Git {
 public:
  struct BranchInfo {
    std::string name;
    bool is_remote = false;
  };

  struct CommitInfo {
    std::vector<std::string> tags;
    std::vector<BranchInfo> branches;
    // True when the commit is decorated with "refs/stash", i.e. it is the tip
    // of the stash stack.
    bool is_stash = false;
    std::string commit;
    // Simplified parents (--parents rewrites these for path-filtered views
    // to only include commits in the output). Use "commit^" for actual parent.
    std::vector<std::string> parents;
    std::string author;
    std::string author_date;
    std::string committer;
    std::string committer_date;
    std::vector<std::string> message;
  };

  explicit GitLog(std::function<void(bool)> busy_callback);
  ~GitLog() final;

  GitLog(GitLog const&) = delete;
  GitLog& operator=(GitLog const&) = delete;

  void SetExtraArgs(std::vector<std::string> args);
  void SetPathFilter(std::vector<std::string> args);
  void ClearPathFilter() {
    path_filter_.clear();
    std::erase(extra_args_, "--follow");
  }
  bool HasPathFilter() const { return !path_filter_.empty(); }

  bool Run();

  void Update();

  bool DidClear() const { return did_clear_; }

  std::span<const CommitInfo> GetCommits() const {
    return commit_history_.data();
  }

  std::string head_branch() const {
    std::scoped_lock lock(head_branch_lock_);
    return head_branch_;
  }

  // Main thread: returns true if the last run failed, moving the error message
  // into |error|.
  bool GetError(std::string& error);

  // Main thread: returns true if the last run produced an informational message
  // (e.g. empty repo), moving the message into |message|.
  bool GetInfoMessage(std::string& message);

 private:
  void FlushPendingCommit();

  std::vector<std::string> extra_args_;
  std::vector<std::string> path_filter_;
  DoubleBuffer<CommitInfo> commit_history_;
  mutable std::mutex head_branch_lock_;
  std::string head_branch_;
  bool did_clear_ = false;
  std::optional<CommitInfo> pending_commit_;
  std::vector<std::string> fields_;  // Reusable parse buffer.

  std::mutex error_lock_;
  bool has_error_ = false;
  std::string error_;
  bool has_info_message_ = false;
  std::string info_message_;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status, int result, std::string err) final;
  void OnKilled() final;
};

#endif  // GEL_COMMANDS_GIT_LOG_H
