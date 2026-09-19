// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_diff_tree.h"

#include <sstream>

GitDiffTree::GitDiffTree(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitDiffTree::~GitDiffTree() {
  TerminateWorkerThread();
}

bool GitDiffTree::Run(const std::string& commit_hash) {
  return RunWithArgs(
      {"diff-tree", "-r", "--root", "--no-commit-id", commit_hash});
}

bool GitDiffTree::Update(std::vector<DiffTreeEntry>& result) {
  std::scoped_lock scoped_lock(result_lock_);
  if (!has_result_)
    return false;
  has_result_ = false;
  result = std::move(result_);
  return true;
}

void GitDiffTree::OnStarted() {
  entries_.clear();
}

void GitDiffTree::OnOutput(std::string line) {
  if (line.empty())
    return;
  // Format: ":old_mode new_mode old_hash new_hash status\tpath"
  auto tab_pos = line.find('\t');
  if (tab_pos == std::string::npos)
    return;
  std::string file_path = line.substr(tab_pos + 1);
  std::istringstream meta(line.substr(0, tab_pos));
  std::string old_mode, new_mode, old_hash, new_hash;
  meta >> old_mode >> new_mode >> old_hash >> new_hash;
  // Skip submodule (gitlink) entries -- their commit hashes don't exist
  // in the main repo and cat-file would report them as missing.
  if (new_mode == "160000")
    return;
  bool deleted = (new_hash.find_first_not_of('0') == std::string::npos);
  entries_.push_back({std::move(file_path), std::move(new_hash), deleted});
}

void GitDiffTree::OnFinished(Exec::Status status,
                             int result_code,
                             std::string err) {
  if (status != Exec::Status::EXITED || result_code != 0) {
    entries_.clear();
    return;
  }
  std::scoped_lock scoped_lock(result_lock_);
  result_ = std::move(entries_);
  has_result_ = true;
}

void GitDiffTree::OnKilled() {
  entries_.clear();
}
