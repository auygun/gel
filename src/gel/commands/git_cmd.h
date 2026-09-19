// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_CMD_H
#define GEL_COMMANDS_GIT_CMD_H

#include <mutex>
#include <string>

#include "gel/commands/git.h"
#include "third_party/kaliber/base/timer.h"

// Runs one-shot git commands (e.g. cherry-pick, revert, reset) in the
// background and reports success or failure back to the main thread.
class GitCmd final : public Git {
 public:
  struct Result {
    bool success = false;
    std::string output;
    std::string error;
  };

  explicit GitCmd(std::function<void(bool)> busy_callback);
  ~GitCmd() final;

  GitCmd(GitCmd const&) = delete;
  GitCmd& operator=(GitCmd const&) = delete;

  // Runs "git <args...>" in the background. Captures the subcommand name
  // (skipping a leading "-C <path>") for display purposes.
  bool Run(std::vector<std::string> args);

  // Main thread: returns true if a command completed since the last call,
  // filling |result| with the outcome.
  bool Update(Result& result);

  // Returns the subcommand name of the last Run() call (e.g. "cherry-pick").
  const std::string& cmd_name() const { return cmd_name_; }

  // Returns true if the last Run() call was an --abort command.
  bool is_abort() const { return is_abort_; }

  // Returns true if the last Run() call was a --quit command.
  bool is_quit() const { return is_quit_; }

  // Returns seconds elapsed since the last Run() call.
  double elapsed() const { return run_timer_.Elapsed(); }

 private:
  std::string cmd_name_;
  bool is_abort_ = false;
  bool is_quit_ = false;
  base::ElapsedTimer run_timer_;
  std::string output_;  // Guarded by worker thread (single writer).

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final {}

  std::mutex result_lock_;
  bool has_result_ = false;
  Result result_;
};

#endif  // GEL_COMMANDS_GIT_CMD_H
