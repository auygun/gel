// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/git_repo.h"

#include <string>

#include "base/exec.h"
#include "third_party/kaliber/base/thread_pool.h"

namespace {

std::filesystem::path RunRevParse(const char* arg) {
  Exec proc;
  if (!proc.Start({"git", "rev-parse", arg}))
    return {};
  while (proc.Poll()) {
  }
  if (proc.GetStatus() != Exec::Status::EXITED || proc.GetResult() != 0)
    return {};
  auto& out = proc.GetOut();
  while (!out.empty() && out.back() == '\n')
    out.pop_back();
  return out;
}

}  // namespace

void GitRepo::Init(std::function<void(bool)> callback) {
  toplevel_.clear();
  git_dir_.clear();
  pending_ = 2;
  auto& pool = base::ThreadPool::Get();
  pool.PostTaskAndReplyWithResult<std::filesystem::path>(
      HERE, []() { return RunRevParse("--show-toplevel"); },
      [this, cb = callback](std::filesystem::path result) {
        toplevel_ = std::move(result);
        --pending_;
        if (pending_ == 0 && cb)
          cb(!toplevel_.empty());
      });
  pool.PostTaskAndReplyWithResult<std::filesystem::path>(
      HERE, []() { return RunRevParse("--git-dir"); },
      [this, cb = callback](std::filesystem::path result) {
        git_dir_ = std::move(result);
        --pending_;
        if (pending_ == 0 && cb)
          cb(!toplevel_.empty());
      });
}
