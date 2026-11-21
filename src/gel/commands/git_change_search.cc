// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_change_search.h"

#include <charconv>
#include <utility>

GitChangeSearch::GitChangeSearch(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitChangeSearch::~GitChangeSearch() {
  TerminateWorkerThread();
}

bool GitChangeSearch::Start(int run_id, const Query& query) {
  std::vector<std::string> args = query.git_options;
  // -s suppresses the diff itself, leaving only the commit line diff-tree
  // prints for each commit that has a change left after filtering. --root
  // includes the changes of root commits.
  args.insert(args.end(), {"diff-tree", "-r", "-s", "--root", "--stdin"});
  args.insert(args.end(), query.diff_options.begin(), query.diff_options.end());
  if (!query.pathspecs.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), query.pathspecs.begin(), query.pathspecs.end());
  }
  return RunWithArgs(std::move(args), {}, run_id, /*keep_stdin_open=*/true);
}

void GitChangeSearch::Check(int index, std::string_view commit) {
  // Marker lines start with '#', which diff-tree echoes as they are rather
  // than reading them as commits.
  input_ += commit;
  input_ += "\n#";
  input_ += std::to_string(index);
  input_ += '\n';
}

void GitChangeSearch::Flush() {
  if (input_.empty())
    return;
  WriteStdin(input_);
  input_.clear();
}

void GitChangeSearch::OnStarted() {
  matched_ = false;
  err_.clear();
  events_.Reset();
}

void GitChangeSearch::OnOutput(std::string line) {
  if (line.empty())
    return;
  if (line[0] != '#') {
    // "<commit>": the commit before the next marker matched.
    matched_ = true;
    return;
  }
  int index = 0;
  auto [ptr, ec] =
      std::from_chars(line.data() + 1, line.data() + line.size(), index);
  if (ec != std::errc())
    return;
  events_.Push({run_id(),
                Event::Kind::kChecked,
                false,
                std::exchange(matched_, false),
                index,
                {}});
}

void GitChangeSearch::OnError(std::string line) {
  if (!err_.empty())
    err_ += '\n';
  err_ += line;
}

void GitChangeSearch::OnFinished(Exec::Status status, int result) {
  bool failed = status != Exec::Status::EXITED || result != 0;
  events_.Push({run_id(), Event::Kind::kFinished, failed, false, 0,
                failed ? std::move(err_) : std::string()});
}
