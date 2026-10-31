// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_REFS_H
#define GEL_COMMANDS_GIT_REFS_H

#include <span>
#include <string>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

// Runs "git for-each-ref" to list branches, tags and the stash, along with the
// object each ref points to. Only the ref store is read, so this stays fast on
// a cold cache even in repositories with a huge number of refs. Annotated tags
// point to their tag object rather than the commit (see GitPeelTags).
class GitRefs final : public Git {
 public:
  struct Ref {
    enum class Kind { kBranch, kRemoteBranch, kTag, kStash };

    Kind kind = Kind::kBranch;
    // Short name, e.g. "main", "origin/main" or "v1.0". Empty for the stash.
    std::string name;
    // The object the ref points to.
    std::string object;
    // True for the checked-out branch.
    bool is_head = false;
  };

  explicit GitRefs(std::function<void(bool)> busy_callback);
  ~GitRefs() final;

  GitRefs(GitRefs const&) = delete;
  GitRefs& operator=(GitRefs const&) = delete;

  bool Run();

  void Update();

  // True if a new run started since the previous Update(), in which case
  // GetRefs() starts over with the refs of the new run.
  bool DidClear() const { return did_clear_; }

  // Refs listed so far by the current run.
  std::span<const Ref> GetRefs() const { return buffers_.data<Ref>(); }

  // True once the current run has listed all refs.
  bool IsComplete() const { return !buffers_.data<RunFinished>().empty(); }

 private:
  // Pushed after the last ref of a successful run. Shares the lock with the
  // refs, so all refs of the run have been merged by the time it is seen.
  struct RunFinished {};

  DoubleBuffer<Ref, RunFinished> buffers_;
  bool did_clear_ = false;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_REFS_H
