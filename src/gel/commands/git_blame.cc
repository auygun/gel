// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_blame.h"

#include <cstdlib>

#include "gel/commands/git_ls_tree.h"

using namespace base;

GitBlame::GitBlame(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitBlame::~GitBlame() {
  TerminateWorkerThread();
}

bool GitBlame::Run(const std::string& revision,
                   const std::string& file,
                   int line_number,
                   const std::filesystem::path& toplevel,
                   std::optional<std::string> contents) {
  std::string line_arg =
      "-L" + std::to_string(line_number) + "," + std::to_string(line_number);
  std::vector<std::string> args = {"blame", "--porcelain", line_arg};
  if (contents)
    args.insert(args.end(), {"--contents", "-"});
  if (!revision.empty())
    args.push_back(revision);
  args.insert(args.end(), {"--", file});
  if (!toplevel.empty())
    args.insert(args.begin(), {"-C", toplevel.string()});
  // Written to stdin, which is then closed. Empty contents have no line to
  // blame: git fails on the -L range without waiting for input.
  return RunWithArgs(std::move(args), contents ? std::move(*contents) : "");
}

bool GitBlame::Update(Result& result) {
  std::scoped_lock lock(result_lock_);
  if (!has_result_)
    return false;
  result = std::move(result_);
  has_result_ = false;
  return true;
}

void GitBlame::OnStarted() {
  first_line_ = true;
  parse_hash_.clear();
  parse_file_.clear();
  parse_line_ = 0;
}

void GitBlame::OnOutput(std::string line) {
  if (first_line_) {
    first_line_ = false;
    size_t space = line.find(' ');
    parse_hash_ = line.substr(0, space);
    if (space != std::string::npos)
      parse_line_ = std::atoi(line.c_str() + space + 1);
  }
  // Quoted if it has special characters, as in ls-tree's output.
  if (line.starts_with("filename "))
    parse_file_ = UnquoteGitPath(std::string_view(line).substr(9));
}

void GitBlame::OnFinished(Exec::Status status, int exit_code) {
  std::scoped_lock lock(result_lock_);
  result_.success = (status == Exec::Status::EXITED && exit_code == 0 &&
                     !parse_hash_.empty());
  result_.hash = std::move(parse_hash_);
  result_.file = std::move(parse_file_);
  result_.line = parse_line_;
  has_result_ = true;
}

void GitBlame::OnKilled() {}
