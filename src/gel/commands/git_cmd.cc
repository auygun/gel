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
  if (!RunWithArgs(std::move(args)))
    return false;
  running_ = true;
  awaiting_clear_ = true;
  return true;
}

bool GitCmd::Update() {
  did_clear_ = buffers_.Merge();
  if (did_clear_)
    awaiting_clear_ = false;
  if (!running_ || awaiting_clear_ || buffers_.data<Result>().empty())
    return false;
  running_ = false;
  return true;
}

void GitCmd::OnStarted() {
  buffers_.Reset();
}

void GitCmd::OnOutput(std::string line) {
  buffers_.Push(std::move(line));
}

void GitCmd::OnFinished(Exec::Status status, int exit_code, std::string err) {
  buffers_.Push<Result>(
      {status == Exec::Status::EXITED && exit_code == 0, std::move(err)});
}
