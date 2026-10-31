// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_REF_DECORATIONS_H
#define GEL_REF_DECORATIONS_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gel/commands/git_peel_tags.h"
#include "gel/commands/git_refs.h"

// Branch, tag and stash labels for commits, loaded separately from git log.
// Letting git log decorate its output (%d) makes it read every ref's object
// before printing the first commit, which takes seconds on a cold cache in
// repositories with many refs. Instead, refs are listed by GitRefs and
// annotated tags are resolved to their commits in the background by
// GitPeelTags. Both stream their results, so labels show up as soon as they
// are available. Resolved tags are cached, so later refreshes only resolve new
// tags.
class RefDecorations {
 public:
  struct BranchInfo {
    std::string name;
    bool is_remote = false;
  };

  struct Decoration {
    std::vector<std::string> tags;
    std::vector<BranchInfo> branches;
    // True when "refs/stash" points here, i.e. the tip of the stash stack.
    bool is_stash = false;
  };

  explicit RefDecorations(std::function<void(bool)> busy_callback);
  ~RefDecorations();

  RefDecorations(RefDecorations const&) = delete;
  RefDecorations& operator=(RefDecorations const&) = delete;

  // Starts reloading the refs.
  void Run();

  // Main thread: merges new data from the background workers.
  void Update();

  // Returns the labels for |commit|, or nullptr if it has none.
  const Decoration* Find(const std::string& commit) const;

  // The checked-out branch, empty when HEAD is detached.
  const std::string& head_branch() const { return head_branch_; }

 private:
  using Ref = GitRefs::Ref;

  struct RefKey {
    Ref::Kind kind;
    std::string name;

    bool operator==(const RefKey&) const = default;
  };

  struct RefKeyHash {
    size_t operator()(const RefKey& key) const {
      return std::hash<std::string>()(key.name) ^ static_cast<size_t>(key.kind);
    }
  };

  struct RefState {
    // The object the ref points to.
    std::string object;
    // The GitRefs run that last listed the ref.
    uint32_t generation = 0;
  };

  GitRefs git_refs_;
  GitPeelTags git_peel_tags_;

  // Labels keyed by commit. Annotated tags stay keyed by their tag object
  // until resolved.
  std::unordered_map<std::string, Decoration> decorations_;
  std::string head_branch_;

  // All known refs. Refs not listed again by a completed GitRefs run have been
  // deleted and their labels are removed.
  std::unordered_map<RefKey, RefState, RefKeyHash> refs_;

  // Tag object -> peeled object. Empty if it could not be resolved. Persists
  // across runs since tag objects are immutable.
  std::unordered_map<std::string, std::string> peeled_;

  // State of the current GitRefs run.
  uint32_t generation_ = 0;
  size_t refs_consumed_ = 0;
  bool run_complete_ = false;
  std::string run_head_branch_;
  std::vector<std::string> unresolved_;
  std::unordered_set<std::string> unresolved_set_;

  // Number of results of the current GitPeelTags run already applied.
  size_t peeled_consumed_ = 0;

  void ApplyRef(const Ref& ref);
  void OnRunComplete();

  // Returns the commit the labels of refs pointing to |object| are shown on.
  const std::string& Locate(const std::string& object) const;

  void AddLabel(const std::string& object, const RefKey& key);
  void RemoveLabel(const std::string& object, const RefKey& key);

  // Moves the labels keyed by tag object |object| to |peeled|.
  void ApplyPeeled(const std::string& object, const std::string& peeled);
};

#endif  // GEL_REF_DECORATIONS_H
