// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_cmd.h"

using namespace base;

GitCmd::GitCmd(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitCmd::~GitCmd() {
  TerminateWorkerThread();
}

bool GitCmd::Run(std::vector<std::string> args) {
  // Skip a leading "-C <path>" global option (prepended by GitCmdRunner)
  // so the subcommand name and flags are captured from the actual
  // subcommand.
  size_t offset =
      (!args.empty() && args[0] == "-C" && args.size() >= 2) ? 2 : 0;
  cmd_name_ = args.size() > offset ? args[offset] : "";
  is_abort_ = args.size() > offset + 1 && args[offset + 1] == "--abort";
  is_quit_ = args.size() > offset + 1 && args[offset + 1] == "--quit";
  run_timer_ = {};
  return RunWithArgs(std::move(args));
}

bool GitCmd::Update(Result& result) {
  std::scoped_lock lock(result_lock_);
  if (!has_result_)
    return false;
  result = std::move(result_);
  has_result_ = false;
  return true;
}

void GitCmd::OnStarted() {
  output_.clear();
}

void GitCmd::OnOutput(std::string line) {
  if (!output_.empty())
    output_ += '\n';
  output_ += std::move(line);
}

void GitCmd::OnFinished(Exec::Status status, int exit_code, std::string err) {
  std::scoped_lock lock(result_lock_);
  result_.success = (status == Exec::Status::EXITED && exit_code == 0);
  result_.output = std::move(output_);
  result_.error = std::move(err);
  has_result_ = true;
}
