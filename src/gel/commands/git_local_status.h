// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_LOCAL_STATUS_H
#define GEL_COMMANDS_GIT_LOCAL_STATUS_H

#include <mutex>
#include <string>
#include <vector>

#include "gel/commands/git.h"

// Checks for local changes (unstaged or staged) in the background.
// Runs "git status --porcelain" and parses the two-character status prefix.
class GitLocalStatus final : public Git {
 public:
  explicit GitLocalStatus(std::function<void(bool)> busy_callback);
  ~GitLocalStatus() final;

  GitLocalStatus(GitLocalStatus const&) = delete;
  GitLocalStatus& operator=(GitLocalStatus const&) = delete;

  // Sets a path filter (e.g. {"src/foo.cc"}) appended to every command.
  void SetPathFilter(std::vector<std::string> args);
  void ClearPathFilter() { path_filter_.clear(); }
  bool HasPathFilter() const { return !path_filter_.empty(); }

  // Start a local changes check.
  bool Run();

  // Main thread: returns true if a check completed since the last call.
  // Swaps results from the worker thread into the main-thread accessors below.
  bool Update();

  bool has_unstaged() const { return has_unstaged_; }
  bool has_staged() const { return has_staged_; }

 private:
  std::vector<std::string> path_filter_;

  // Main-thread state, updated by Update().
  bool has_unstaged_ = false;
  bool has_staged_ = false;

  // Worker-thread state, protected by result_lock_.
  std::mutex result_lock_;
  bool has_result_ = false;
  bool result_has_unstaged_ = false;
  bool result_has_staged_ = false;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_LOCAL_STATUS_H
