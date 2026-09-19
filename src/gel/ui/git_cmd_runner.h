// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_GIT_CMD_RUNNER_H
#define GEL_UI_GIT_CMD_RUNNER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gel/commands/git_cmd.h"

class GitRepo;

enum class GitTask { kNone, kCherryPick, kRevert, kRebase, kMerge };
enum class TaskState { kIdle, kCmdRunning, kWaitingForUser };

// Prefix byte for informational/status lines in task output (e.g.
// "Cherry-pick completed."). Stripped during rendering and drawn in a
// distinct color.
constexpr char kInfoLinePrefix = '\x01';

// Returns a human-readable name for the given task.
inline const char* GetTaskName(GitTask task) {
  switch (task) {
    case GitTask::kCherryPick:
      return "Cherry-pick";
    case GitTask::kRevert:
      return "Revert";
    case GitTask::kRebase:
      return "Rebase";
    case GitTask::kMerge:
      return "Merge";
    default:
      return "";
  }
}

// Centralized runner for one-shot git commands. Owns a single serialized
// command slot (cherry-pick, checkout, branch, tag, etc.) and a pool of
// long-lived processes (diff tools, blame, merge tool) that auto-detach after
// a timeout. Tracks in-progress git tasks (cherry-pick, revert, rebase)
// and provides Continue/Abort API.
class GitCmdRunner {
 public:
  // Called when the serialized command slot completes.
  using CmdResultCallback =
      std::function<void(bool success, bool is_cancel, bool local_only)>;
  // Called when a git command finishes with an error. Return true if the
  // error was handled by the caller (e.g. a follow-up dialog was shown) and
  // the console window should not be opened.
  using ErrorCallback = std::function<bool(const std::string& error,
                                           const std::string& cmd_name,
                                           bool show_popup)>;

  GitCmdRunner(GitRepo& git_repo,
               std::function<void(bool)> busy_callback,
               CmdResultCallback cmd_result_callback,
               ErrorCallback error_callback);

  // Runs "git <args...>" in the serialized slot. Returns false if the
  // command slot is occupied. Set |local_only| for commands that only affect
  // the working tree or index (add, restore, clean) so the result callback
  // can skip a full refresh.
  bool Run(std::vector<std::string> args, bool local_only = false);

  // Queues a command to run after the current one finishes successfully.
  // Ignored if no command is running. The result callback fires only after
  // the last command in the chain.
  void RunNext(std::vector<std::string> args);

  // Launches a long-lived git process (diff tool, blame GUI, merge tool).
  // Auto-detaches after 3 seconds. Multiple can run concurrently.
  // When |silent| is true, errors are not reported via the error callback.
  void Spawn(std::vector<std::string> args, bool silent = false);

  // Polls all running commands. Call once per frame.
  void Update();

  // True if a git command is running or a task is in progress.
  bool IsBusy() const;

  // True if the serialized command slot is currently running.
  bool IsCommandBusy() const { return cmd_.busy(); }

  // In-progress task queries.
  GitTask task() const { return task_; }
  TaskState task_state() const { return task_state_; }
  std::string& task_output() { return task_output_; }

  // Continue, skip, abort, or quit the current in-progress task.
  void Continue();
  void Skip();
  void Abort();
  void Quit();

  // Check .git directory for in-progress task markers.
  void UpdateTaskState();

  // Returns true (once) when the console window should be shown.
  bool ConsumeShowConsole();

 private:
  GitRepo& git_repo_;
  std::function<void(bool)> busy_callback_;
  CmdResultCallback cmd_result_callback_;
  ErrorCallback error_callback_;

  GitCmd cmd_;

  struct SpawnedProc {
    std::unique_ptr<GitCmd> cmd;
    bool silent = false;
  };
  std::vector<SpawnedProc> procs_;

  GitTask task_ = GitTask::kNone;
  TaskState task_state_ = TaskState::kIdle;
  std::string task_output_;
  bool show_console_ = false;
  bool local_only_ = false;
  std::vector<std::string> next_args_;

  void PrependTopLevel(std::vector<std::string>& args);
  void RunFollowUp(const char* flag);
  static GitTask ClassifyCommand(const std::string& cmd_name);
};

#endif  // GEL_UI_GIT_CMD_RUNNER_H
