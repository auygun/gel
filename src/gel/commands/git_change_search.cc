// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_change_search.h"

#include <charconv>

GitChangeSearch::GitChangeSearch(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitChangeSearch::~GitChangeSearch() {
  TerminateWorkerThread();
}

bool GitChangeSearch::Run(int run_id,
                          const Query& query,
                          std::span<const GitLog::CommitInfo> commits,
                          std::span<const int> indices) {
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

  // Marker lines start with '#', which diff-tree echoes as they are, flushing
  // its output, rather than reading them as commits.
  auto add_marker = [](std::string& input, size_t position) {
    input += '#';
    input += std::to_string(position);
    input += '\n';
  };
  std::string input;
  if (!indices.empty()) {
    size_t hash_size = commits[indices[0]].commit.size() + 1;
    size_t markers = indices.size() / kMarkerInterval + 2;
    input.reserve(indices.size() * hash_size + markers * 12);
  }
  for (size_t i = 0; i < indices.size(); ++i) {
    if (i % kMarkerInterval == 0)
      add_marker(input, i);
    input += commits[indices[i]].commit;
    input += '\n';
  }
  // Reports the whole list checked once diff-tree has worked through it.
  add_marker(input, indices.size());
  return RunWithArgs(std::move(args), std::move(input), run_id);
}

void GitChangeSearch::OnStarted() {
  marker_position_ = 0;
  err_.clear();
  events_.Reset();
}

void GitChangeSearch::OnOutput(std::string line) {
  if (line.empty())
    return;
  if (line[0] == '#') {
    int position = 0;
    auto [ptr, ec] =
        std::from_chars(line.data() + 1, line.data() + line.size(), position);
    if (ec != std::errc())
      return;
    marker_position_ = position;
    events_.Push({run_id(), Event::Kind::kProgress, false, position, {}});
    return;
  }
  // "<commit>", possibly followed by its parents.
  size_t space = line.find(' ');
  if (space != std::string::npos)
    line.resize(space);
  events_.Push({run_id(), Event::Kind::kMatch, false, marker_position_,
                std::move(line)});
}

void GitChangeSearch::OnError(std::string line) {
  if (!err_.empty())
    err_ += '\n';
  err_ += line;
}

void GitChangeSearch::OnFinished(Exec::Status status, int result) {
  bool failed = status != Exec::Status::EXITED || result != 0;
  events_.Push({run_id(), Event::Kind::kFinished, failed, 0,
                failed ? std::move(err_) : std::string()});
}
