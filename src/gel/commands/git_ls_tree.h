// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_LS_TREE_H
#define GEL_COMMANDS_GIT_LS_TREE_H

#include <atomic>
#include <filesystem>
#include <span>
#include <string>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

// A file in a commit's tree, in the index, or in the working tree.
struct TreeEntry {
  std::string path;  // Relative to the repository's top level.
  // Blob hash of the file's content. Empty for files read from the working
  // tree, and for submodules, which have no content to show. A symlink's
  // content is its target.
  std::string blob;
  bool submodule = false;
  bool symlink = false;
};

// Lists every file of a commit's tree ("git ls-tree -r"), or the files of the
// index and the working tree ("git ls-files --stage", for both: the working
// tree's tracked files are the index's), streamed to the main thread in git's
// sorted path order.
class GitLsTree final : public Git {
 public:
  enum class Source { kCommit, kIndex, kWorkTree };

  explicit GitLsTree(std::function<void(bool)> busy_callback);
  ~GitLsTree() final;

  GitLsTree(GitLsTree const&) = delete;
  GitLsTree& operator=(GitLsTree const&) = delete;

  // Lists the files of |source|: the tree of |commit| for kCommit. Paths are
  // relative to |toplevel|, where git is run if it isn't empty. The entries of
  // the previous run are dropped right away.
  void Run(Source source,
           const std::string& commit,
           const std::filesystem::path& toplevel);

  // Stops listing and drops the entries.
  void Stop();

  // Main thread: merges the entries listed since the last call.
  void Update();

  // Main thread: the entries of the latest run merged so far, in git's sorted
  // path order.
  std::span<const TreeEntry> entries() const {
    return valid_ ? buffer_.data() : std::span<const TreeEntry>();
  }

  // Main thread: true once all entries of the latest run are merged, or it
  // failed or was stopped.
  bool done() const { return done_; }

  // Main thread: changes with each Run() and Stop(), when entries() starts
  // over.
  int generation() const { return last_run_; }

 private:
  // Main thread state handed to the worker with each run.
  std::mutex params_lock_;
  Source pending_source_ = Source::kCommit;
  // Worker thread.
  Source source_ = Source::kCommit;
  // Path of the last entry pushed, to list a conflicted file once.
  std::string last_path_;

  DoubleBuffer<TreeEntry> buffer_;

  // Main thread: the latest run's id, and its state as of the last Update().
  // The buffer can still hold entries of an earlier run until the latest one
  // starts.
  int last_run_ = 0;
  bool valid_ = false;
  bool done_ = true;
  // Worker thread: the run that started, and finished, last.
  std::atomic<int> started_run_{0};
  std::atomic<int> finished_run_{0};

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnError(std::string) final {}
  void OnFinished(Exec::Status, int) final;
  void OnKilled() final {}
};

// Unquotes a path git quoted for containing special characters (see
// core.quotePath): "a\tb" or "\303\274.txt". Other paths are returned as is.
std::string UnquoteGitPath(std::string_view path);

#endif  // GEL_COMMANDS_GIT_LS_TREE_H
