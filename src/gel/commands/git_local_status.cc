// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_local_status.h"

using namespace base;

GitLocalStatus::GitLocalStatus(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitLocalStatus::~GitLocalStatus() {
  TerminateWorkerThread();
}

void GitLocalStatus::SetPathFilter(std::vector<std::string> args) {
  path_filter_ = std::move(args);
}

bool GitLocalStatus::Run() {
  std::vector<std::string> args = {"status", "--porcelain", "-uall"};
  if (!path_filter_.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), path_filter_.begin(), path_filter_.end());
  }
  return RunWithArgs(std::move(args));
}

bool GitLocalStatus::Update() {
  std::scoped_lock lock(result_lock_);
  if (!has_result_)
    return false;
  has_result_ = false;
  has_unstaged_ = result_has_unstaged_;
  has_staged_ = result_has_staged_;
  return true;
}

void GitLocalStatus::OnStarted() {
  result_has_unstaged_ = false;
  result_has_staged_ = false;
}

void GitLocalStatus::OnOutput(std::string line) {
  if (line.size() < 4)
    return;

  char x = line[0];
  char y = line[1];

  bool is_conflict =
      x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');

  if (is_conflict) {
    result_has_unstaged_ = true;
  } else {
    if (x != ' ' && x != '?')
      result_has_staged_ = true;
    if (y != ' ')
      result_has_unstaged_ = true;
  }
}

void GitLocalStatus::OnFinished(Exec::Status status,
                                int exit_code,
                                std::string err) {
  std::scoped_lock lock(result_lock_);
  has_result_ = true;
}
