// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_CMD_H
#define GEL_COMMANDS_GIT_CMD_H

#include <span>
#include <string>

#include "base/double_buffer.h"
#include "gel/commands/git.h"
#include "third_party/kaliber/base/timer.h"

// Runs one-shot git commands (e.g. cherry-pick, revert, reset) in the
// background. Streams the output lines and reports success or failure back to
// the main thread.
class GitCmd final : public Git {
 public:
  struct Result {
    bool success = false;
    std::string error;
  };

  explicit GitCmd(std::function<void(bool)> busy_callback);
  ~GitCmd() final;

  GitCmd(GitCmd const&) = delete;
  GitCmd& operator=(GitCmd const&) = delete;

  // Runs "git <args...>" in the background. Captures the subcommand name
  // (skipping a leading "-C <path>") for display purposes.
  bool Run(std::vector<std::string> args);

  // Main thread: merges data streamed by the worker. Call once per frame.
  // Returns true if a command completed since the last call.
  bool Update();

  // Main thread: true if the last Update() cleared the output of the previous
  // command.
  bool DidClear() const { return did_clear_; }

  // Main thread: stdout lines of the current or last command.
  std::span<const std::string> GetOutput() const {
    return buffers_.data<std::string>();
  }

  // Main thread: outcome of the last command. Only valid after Update()
  // returned true.
  const Result& GetResult() const { return buffers_.data<Result>().back(); }

  // Returns the subcommand name of the last Run() call (e.g. "cherry-pick").
  const std::string& cmd_name() const { return cmd_name_; }

  // Returns true if the last Run() call was an --abort command.
  bool is_abort() const { return is_abort_; }

  // Returns true if the last Run() call was a --quit command.
  bool is_quit() const { return is_quit_; }

  // Main thread: true from a successful Run() call until Update() returns true
  // for it. Unlike busy(), stays true until the result has been consumed.
  bool running() const { return running_; }

  // Returns seconds elapsed since the last Run() call.
  double elapsed() const { return run_timer_.Elapsed(); }

 private:
  std::string cmd_name_;
  bool is_abort_ = false;
  bool is_quit_ = false;
  base::ElapsedTimer run_timer_;
  // Output lines and the result (pushed once when the command finishes).
  DoubleBuffer<std::string, Result> buffers_;
  bool did_clear_ = false;
  bool running_ = false;
  // Set by Run() until the worker's Reset() has been merged, so that the
  // previous command's result is not mistaken for the new one's.
  bool awaiting_clear_ = false;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_CMD_H
