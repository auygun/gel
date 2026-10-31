// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ref_decorations.h"

#include <algorithm>

RefDecorations::RefDecorations(std::function<void(bool)> busy_callback)
    : git_refs_(busy_callback), git_peel_tags_(busy_callback) {}

RefDecorations::~RefDecorations() = default;

void RefDecorations::Run() {
  git_refs_.Run();
}

void RefDecorations::Update() {
  git_refs_.Update();
  if (git_refs_.DidClear()) {
    ++generation_;
    refs_consumed_ = 0;
    run_complete_ = false;
    run_head_branch_.clear();
    unresolved_.clear();
    unresolved_set_.clear();
  }
  auto refs = git_refs_.GetRefs();
  for (; refs_consumed_ < refs.size(); ++refs_consumed_)
    ApplyRef(refs[refs_consumed_]);
  if (!run_complete_ && git_refs_.IsComplete()) {
    run_complete_ = true;
    OnRunComplete();
  }

  git_peel_tags_.Update();
  if (git_peel_tags_.DidClear())
    peeled_consumed_ = 0;
  auto peeled = git_peel_tags_.GetPeeled();
  for (; peeled_consumed_ < peeled.size(); ++peeled_consumed_) {
    const auto& [object, target] = peeled[peeled_consumed_];
    // A superseded run may have resolved the object already.
    if (peeled_.try_emplace(object, target).second)
      ApplyPeeled(object, target);
  }
}

const RefDecorations::Decoration* RefDecorations::Find(
    const std::string& commit) const {
  if (decorations_.empty())
    return nullptr;
  auto it = decorations_.find(commit);
  return it == decorations_.end() ? nullptr : &it->second;
}

void RefDecorations::ApplyRef(const Ref& ref) {
  RefKey key{ref.kind, ref.name};
  auto [it, inserted] = refs_.try_emplace(key);
  RefState& state = it->second;
  if (inserted || state.object != ref.object) {
    if (!inserted)
      RemoveLabel(state.object, key);
    state.object = ref.object;
    AddLabel(state.object, key);
  }
  state.generation = generation_;

  if (ref.is_head) {
    head_branch_ = ref.name;
    run_head_branch_ = ref.name;
  }
  if (ref.kind == Ref::Kind::kTag && !peeled_.contains(ref.object) &&
      unresolved_set_.insert(ref.object).second) {
    unresolved_.push_back(ref.object);
  }
}

void RefDecorations::OnRunComplete() {
  // Refs not listed by the completed run have been deleted.
  for (auto it = refs_.begin(); it != refs_.end();) {
    if (it->second.generation != generation_) {
      RemoveLabel(it->second.object, it->first);
      it = refs_.erase(it);
    } else {
      ++it;
    }
  }
  head_branch_ = std::move(run_head_branch_);

  // Results of a previous run may have arrived since the objects were listed.
  std::erase_if(unresolved_, [this](const std::string& object) {
    return peeled_.contains(object);
  });
  // Restarting a run that is still in progress loses nothing, since all
  // objects still unresolved are requested again.
  if (!unresolved_.empty())
    git_peel_tags_.Run(unresolved_);
  unresolved_.clear();
  unresolved_set_.clear();
}

const std::string& RefDecorations::Locate(const std::string& object) const {
  auto it = peeled_.find(object);
  if (it == peeled_.end() || it->second.empty())
    return object;
  return it->second;
}

void RefDecorations::AddLabel(const std::string& object, const RefKey& key) {
  Decoration& decoration = decorations_[Locate(object)];
  switch (key.kind) {
    case Ref::Kind::kTag: {
      auto& tags = decoration.tags;
      tags.insert(std::upper_bound(tags.begin(), tags.end(), key.name),
                  key.name);
      break;
    }
    case Ref::Kind::kBranch:
    case Ref::Kind::kRemoteBranch: {
      auto& branches = decoration.branches;
      auto pos = std::upper_bound(
          branches.begin(), branches.end(), key.name,
          [](const std::string& name, const BranchInfo& branch) {
            return name < branch.name;
          });
      branches.insert(pos, {key.name, key.kind == Ref::Kind::kRemoteBranch});
      break;
    }
    case Ref::Kind::kStash:
      decoration.is_stash = true;
      break;
  }
}

void RefDecorations::RemoveLabel(const std::string& object, const RefKey& key) {
  auto it = decorations_.find(Locate(object));
  if (it == decorations_.end())
    return;
  Decoration& decoration = it->second;
  switch (key.kind) {
    case Ref::Kind::kTag:
      std::erase(decoration.tags, key.name);
      break;
    case Ref::Kind::kBranch:
    case Ref::Kind::kRemoteBranch: {
      bool is_remote = key.kind == Ref::Kind::kRemoteBranch;
      std::erase_if(decoration.branches, [&](const BranchInfo& branch) {
        return branch.name == key.name && branch.is_remote == is_remote;
      });
      break;
    }
    case Ref::Kind::kStash:
      decoration.is_stash = false;
      break;
  }
  if (decoration.tags.empty() && decoration.branches.empty() &&
      !decoration.is_stash) {
    decorations_.erase(it);
  }
}

void RefDecorations::ApplyPeeled(const std::string& object,
                                 const std::string& peeled) {
  // Lightweight tags already point to the commit.
  if (peeled.empty() || peeled == object)
    return;
  auto node = decorations_.extract(object);
  if (node.empty())
    return;
  Decoration& src = node.mapped();
  Decoration& dst = decorations_[peeled];
  dst.tags.insert(dst.tags.end(), std::make_move_iterator(src.tags.begin()),
                  std::make_move_iterator(src.tags.end()));
  std::sort(dst.tags.begin(), dst.tags.end());
  if (!src.branches.empty()) {
    dst.branches.insert(dst.branches.end(),
                        std::make_move_iterator(src.branches.begin()),
                        std::make_move_iterator(src.branches.end()));
    std::sort(dst.branches.begin(), dst.branches.end(),
              [](const BranchInfo& a, const BranchInfo& b) {
                return a.name < b.name;
              });
  }
  dst.is_stash |= src.is_stash;
}
