// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_ls_tree.h"

namespace {

// Modes of a submodule (gitlink) and of a symlink entry.
constexpr std::string_view kGitlinkMode = "160000";
constexpr std::string_view kSymlinkMode = "120000";

}  // namespace

std::string UnquoteGitPath(std::string_view path) {
  if (path.size() < 2 || path.front() != '"' || path.back() != '"')
    return std::string(path);
  path = path.substr(1, path.size() - 2);
  std::string result;
  result.reserve(path.size());
  for (size_t i = 0; i < path.size(); i++) {
    char c = path[i];
    if (c != '\\' || i + 1 == path.size()) {
      result += c;
      continue;
    }
    char e = path[++i];
    switch (e) {
      case 'a':
        result += '\a';
        break;
      case 'b':
        result += '\b';
        break;
      case 'f':
        result += '\f';
        break;
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      case 'v':
        result += '\v';
        break;
      default:
        if (e >= '0' && e <= '3' && i + 2 < path.size() && path[i + 1] >= '0' &&
            path[i + 1] <= '7' && path[i + 2] >= '0' && path[i + 2] <= '7') {
          // Three octal digits: a byte of a UTF-8 sequence.
          unsigned byte = (static_cast<unsigned>(e - '0') << 6U) |
                          (static_cast<unsigned>(path[i + 1] - '0') << 3U) |
                          static_cast<unsigned>(path[i + 2] - '0');
          result += static_cast<char>(byte);
          i += 2;
        } else {
          result += e;  // '\\' or '"'.
        }
        break;
    }
  }
  return result;
}

GitLsTree::GitLsTree(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitLsTree::~GitLsTree() {
  TerminateWorkerThread();
}

void GitLsTree::Run(Source source,
                    const std::string& commit,
                    const std::filesystem::path& toplevel) {
  int run = ++last_run_;
  valid_ = false;
  done_ = false;
  {
    std::scoped_lock lock(params_lock_);
    pending_source_ = source;
  }
  std::vector<std::string> args;
  if (!toplevel.empty())
    args = {"-C", toplevel.string()};
  switch (source) {
    case Source::kCommit:
      args.insert(args.end(), {"ls-tree", "-r", commit});
      break;
    case Source::kIndex:
      args.insert(args.end(), {"ls-files", "--stage"});
      break;
    case Source::kWorkTree:
      // With --stage, to tell submodules from files.
      args.insert(args.end(), {"ls-files", "--stage"});
      break;
  }
  if (!RunWithArgs(std::move(args), {}, run))
    done_ = true;
}

void GitLsTree::Stop() {
  ++last_run_;
  valid_ = false;
  done_ = true;
  Kill();
}

void GitLsTree::Update() {
  // Read before merging: a run's entries are all pushed before it reports
  // finishing, and its buffer reset before it reports starting, so the merge
  // below takes them in.
  int started = started_run_.load(std::memory_order_acquire);
  int finished = finished_run_.load(std::memory_order_acquire);
  buffer_.Merge();
  if (started == last_run_)
    valid_ = true;
  if (finished == last_run_)
    done_ = true;
}

void GitLsTree::OnStarted() {
  {
    std::scoped_lock lock(params_lock_);
    source_ = pending_source_;
  }
  buffer_.Reset();
  last_path_.clear();
  started_run_.store(run_id(), std::memory_order_release);
}

void GitLsTree::OnFinished(Exec::Status, int) {
  finished_run_.store(run_id(), std::memory_order_release);
}

void GitLsTree::OnOutput(std::string line) {
  // ls-tree:  "<mode> <type> <hash>\t<path>"
  // ls-files: "<mode> <hash> <stage>\t<path>"
  size_t tab = line.find('\t');
  if (tab == std::string::npos)
    return;
  std::string_view meta(line.data(), tab);
  size_t sp1 = meta.find(' ');
  size_t sp2 = sp1 == std::string_view::npos ? sp1 : meta.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos)
    return;
  std::string_view mode = meta.substr(0, sp1);
  std::string_view hash = source_ == Source::kCommit
                              ? meta.substr(sp2 + 1)
                              : meta.substr(sp1 + 1, sp2 - sp1 - 1);
  TreeEntry entry;
  entry.path = UnquoteGitPath(std::string_view(line).substr(tab + 1));
  if (source_ != Source::kCommit) {
    // An unmerged file is listed once per stage it has (1: base, 2: ours, 3:
    // theirs), in that order. Keep one entry: the working tree shows the
    // conflict and the index has no single version. The first stage, as a
    // file deleted on one side lacks the other side's stage.
    if (entry.path == last_path_)
      return;
    last_path_ = entry.path;
  }
  entry.submodule = mode == kGitlinkMode;
  entry.symlink = mode == kSymlinkMode;
  if (!entry.submodule && source_ != Source::kWorkTree)
    entry.blob = hash;
  buffer_.Push(std::move(entry));
}
