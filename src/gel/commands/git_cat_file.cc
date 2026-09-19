// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_cat_file.h"

GitCatFile::GitCatFile(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitCatFile::~GitCatFile() {
  TerminateWorkerThread();
}

bool GitCatFile::Run(const std::vector<std::string>& hashes) {
  std::string input;
  for (const auto& hash : hashes) {
    if (!input.empty())
      input += '\n';
    input += hash;
  }
  return RunWithArgs({"cat-file", "--batch-check=%(objectsize)"},
                     std::move(input));
}

bool GitCatFile::Update(std::vector<int64_t>& sizes) {
  std::scoped_lock scoped_lock(result_lock_);
  if (!has_result_)
    return false;
  has_result_ = false;
  sizes = std::move(result_);
  return true;
}

void GitCatFile::OnStarted() {
  sizes_.clear();
}

void GitCatFile::OnOutput(std::string line) {
  int64_t sz = 0;
  try {
    size_t pos = 0;
    sz = std::stoll(line, &pos);
    // Reject lines that aren't purely numeric (e.g. "<hash> missing").
    if (pos != line.size())
      sz = 0;
  } catch (...) {
    sz = 0;
  }
  sizes_.push_back(sz);
}

void GitCatFile::OnFinished(Exec::Status status,
                            int result_code,
                            std::string err) {
  if (status != Exec::Status::EXITED || result_code != 0) {
    sizes_.clear();
    return;
  }
  std::scoped_lock scoped_lock(result_lock_);
  result_ = std::move(sizes_);
  has_result_ = true;
}

void GitCatFile::OnKilled() {
  sizes_.clear();
}
