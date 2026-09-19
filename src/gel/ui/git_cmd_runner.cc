// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/git_cmd_runner.h"

#include "gel/git_repo.h"

void GitCmdRunner::PrependTopLevel(std::vector<std::string>& args) {
  auto& toplevel = git_repo_.toplevel();
  if (!toplevel.empty())
    args.insert(args.begin(), {"-C", toplevel.string()});
}

GitCmdRunner::GitCmdRunner(GitRepo& git_repo,
                           std::function<void(bool)> busy_callback,
                           CmdResultCallback cmd_result_callback,
                           ErrorCallback error_callback)
    : git_repo_(git_repo),
      busy_callback_(busy_callback),
      cmd_result_callback_(std::move(cmd_result_callback)),
      error_callback_(std::move(error_callback)),
      cmd_(std::move(busy_callback)) {}

GitTask GitCmdRunner::ClassifyCommand(const std::string& cmd_name) {
  if (cmd_name == "cherry-pick")
    return GitTask::kCherryPick;
  if (cmd_name == "revert")
    return GitTask::kRevert;
  if (cmd_name == "rebase")
    return GitTask::kRebase;
  if (cmd_name == "merge")
    return GitTask::kMerge;
  return GitTask::kNone;
}

bool GitCmdRunner::Run(std::vector<std::string> args, bool local_only) {
  if (cmd_.busy())
    return false;

  local_only_ = local_only;
  std::string cmd_name = args.empty() ? "" : args[0];
  GitTask task = ClassifyCommand(cmd_name);

  if (task != GitTask::kNone) {
    if (task_ != GitTask::kNone) {
      // A task is already in progress. Only allow
      // --continue/--skip/--abort/--quit for the same task type.
      bool is_followup =
          args.size() >= 2 && (args[1] == "--continue" || args[1] == "--skip" ||
                               args[1] == "--abort" || args[1] == "--quit");
      if (!is_followup || task != task_)
        return false;
    } else {
      task_ = task;
    }
    task_state_ = TaskState::kCmdRunning;
  }

  PrependTopLevel(args);

  // Add command to output for all commands
  task_output_ += '\n';
  task_output_ += "$ git";
  for (auto& arg : args)
    task_output_ += ' ' + arg;

  return cmd_.Run(std::move(args));
}

void GitCmdRunner::RunNext(std::vector<std::string> args) {
  next_args_ = std::move(args);
}

void GitCmdRunner::Spawn(std::vector<std::string> args, bool silent) {
  PrependTopLevel(args);
  auto& entry = procs_.emplace_back(
      SpawnedProc{std::make_unique<GitCmd>(busy_callback_), silent});
  entry.cmd->Run(std::move(args));
}

void GitCmdRunner::Update() {
  {
    GitCmd::Result result;
    if (cmd_.Update(result)) {
      bool resolved_externally = false;
      // Track output for all commands.
      if (!result.output.empty()) {
        if (!task_output_.empty())
          task_output_ += '\n';
        task_output_ += result.output;
      }
      if (!result.error.empty()) {
        if (!task_output_.empty())
          task_output_ += '\n';
        task_output_ += result.error;
      }

      if (task_ != GitTask::kNone &&
          ClassifyCommand(cmd_.cmd_name()) != GitTask::kNone) {
        if (!result.success)
          show_console_ = true;
        // Fallback: if --continue fails with "nothing to commit", the
        // revert was committed externally and REVERT_HEAD is stale.
        // (Primary detection is the MERGE_MSG check in UpdateTaskState.)
        resolved_externally =
            !result.success && task_ == GitTask::kRevert &&
            result.output.find("nothing to commit") != std::string::npos;
        if (resolved_externally)
          std::filesystem::remove(git_repo_.git_dir() / "REVERT_HEAD");
        GitTask prev_task = task_;
        UpdateTaskState();
        if (prev_task != GitTask::kNone && task_ == GitTask::kNone) {
          if (!task_output_.empty())
            task_output_ += '\n';
          task_output_ += kInfoLinePrefix;
          if (cmd_.is_abort())
            task_output_ += std::string(GetTaskName(prev_task)) + " aborted.";
          else if (cmd_.is_quit())
            task_output_ += std::string(GetTaskName(prev_task)) + " cancelled.";
          else if (resolved_externally)
            task_output_ += std::string(GetTaskName(prev_task)) +
                            " already completed (resolved externally).";
          else if (result.success)
            task_output_ += std::string(GetTaskName(prev_task)) + " completed.";
          else
            task_output_ += std::string(GetTaskName(prev_task)) + " failed.";
        }
      } else if (!result.success) {
        next_args_.clear();
        std::string error = result.error;
        if (error.empty())
          error = result.output;
        if (!error_callback_(error, cmd_.cmd_name(), false))
          show_console_ = true;
      }

      if (result.success && !next_args_.empty()) {
        PrependTopLevel(next_args_);
        cmd_.Run(std::move(next_args_));
        next_args_.clear();
      } else {
        next_args_.clear();
        cmd_result_callback_(
            result.success || resolved_externally,
            (cmd_.is_abort() || cmd_.is_quit()) && !resolved_externally,
            local_only_);
      }
    }
  }

  for (auto it = procs_.begin(); it != procs_.end();) {
    GitCmd::Result result;
    if (it->cmd->Update(result)) {
      if (!result.success && !it->silent)
        error_callback_(result.error, it->cmd->cmd_name(), true);
      it = procs_.erase(it);
    } else if (it->cmd->detached()) {
      it = procs_.erase(it);
    } else if (it->cmd->busy() && it->cmd->elapsed() > 3.0) {
      it->cmd->Detach();
      ++it;
    } else {
      ++it;
    }
  }
}

bool GitCmdRunner::IsBusy() const {
  return cmd_.busy() || task_ != GitTask::kNone;
}

void GitCmdRunner::Continue() {
  RunFollowUp("--continue");
}

void GitCmdRunner::Skip() {
  RunFollowUp("--skip");
}

void GitCmdRunner::Abort() {
  RunFollowUp("--abort");
}

void GitCmdRunner::Quit() {
  RunFollowUp("--quit");
}

void GitCmdRunner::RunFollowUp(const char* flag) {
  GitTask prev_task = task_;
  UpdateTaskState();
  if (task_ == GitTask::kNone) {
    if (prev_task != GitTask::kNone) {
      if (!task_output_.empty())
        task_output_ += '\n';
      task_output_ += kInfoLinePrefix + std::string(GetTaskName(prev_task)) +
                      " already completed (resolved externally).";
      cmd_result_callback_(true, false, false);
    }
    return;
  }
  if (task_state_ != TaskState::kWaitingForUser)
    return;

  const char* cmd = nullptr;
  switch (task_) {
    case GitTask::kCherryPick:
      cmd = "cherry-pick";
      break;
    case GitTask::kRevert:
      cmd = "revert";
      break;
    case GitTask::kRebase:
      cmd = "rebase";
      break;
    case GitTask::kMerge:
      cmd = "merge";
      break;
    default:
      return;
  }
  Run({cmd, flag});
}

void GitCmdRunner::UpdateTaskState() {
  namespace fs = std::filesystem;
  auto& git_dir = git_repo_.git_dir();
  if (git_dir.empty())
    return;

  bool rebase = fs::exists(git_dir / "rebase-merge") ||
                fs::exists(git_dir / "rebase-apply");
  bool cherry = fs::exists(git_dir / "CHERRY_PICK_HEAD");
  bool revert = fs::exists(git_dir / "REVERT_HEAD");
  bool merge = fs::exists(git_dir / "MERGE_HEAD");
  // git commit removes CHERRY_PICK_HEAD but not REVERT_HEAD.
  // Detect stale REVERT_HEAD: if MERGE_MSG was consumed by an external
  // commit, REVERT_HEAD is leftover and should be cleaned up.
  if (revert && !fs::exists(git_dir / "MERGE_MSG")) {
    fs::remove(git_dir / "REVERT_HEAD");
    revert = false;
  }

  if (cherry)
    task_ = GitTask::kCherryPick;
  else if (revert)
    task_ = GitTask::kRevert;
  else if (rebase)
    task_ = GitTask::kRebase;
  else if (merge)
    task_ = GitTask::kMerge;
  else
    task_ = GitTask::kNone;

  if (task_ != GitTask::kNone && !cmd_.busy())
    task_state_ = TaskState::kWaitingForUser;
  else if (task_ == GitTask::kNone)
    task_state_ = TaskState::kIdle;
}

bool GitCmdRunner::ConsumeShowConsole() {
  bool val = show_console_;
  show_console_ = false;
  return val;
}
