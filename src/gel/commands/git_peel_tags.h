// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_PEEL_TAGS_H
#define GEL_COMMANDS_GIT_PEEL_TAGS_H

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

// Runs "git cat-file --batch-check" to resolve the objects tags point to into
// the objects they ultimately reference. Annotated tags point to tag objects,
// which have to be read from the object database. That is slow on a cold cache
// in repositories with many tags, so it runs separately from GitRefs and
// delivers results incrementally.
class GitPeelTags final : public Git {
 public:
  // (object, peeled object). Objects that could not be resolved are paired
  // with an empty string.
  using Peeled = std::pair<std::string, std::string>;

  explicit GitPeelTags(std::function<void(bool)> busy_callback);
  ~GitPeelTags() final;

  GitPeelTags(GitPeelTags const&) = delete;
  GitPeelTags& operator=(GitPeelTags const&) = delete;

  bool Run(const std::vector<std::string>& objects);

  void Update();

  // True if a new run started since the previous Update(), in which case
  // GetPeeled() starts over with the results of the new run.
  bool DidClear() const { return did_clear_; }

  // Objects resolved so far by the current run.
  std::span<const Peeled> GetPeeled() const { return peeled_.data(); }

 private:
  DoubleBuffer<Peeled> peeled_;
  bool did_clear_ = false;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status, int, std::string) final {}
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_PEEL_TAGS_H
