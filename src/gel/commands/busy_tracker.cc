// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/busy_tracker.h"

#include <algorithm>

BusyTracker& BusyTracker::Get() {
  static BusyTracker tracker;
  return tracker;
}

void BusyTracker::Set(const void* owner, std::string command) {
  std::scoped_lock lock(lock_);
  auto [it, inserted] = entries_.try_emplace(owner);
  // A worker replacing its command keeps its place in the list.
  if (inserted)
    it->second.order = next_order_++;
  it->second.command = std::move(command);
  count_.store(static_cast<int>(entries_.size()), std::memory_order_relaxed);
}

void BusyTracker::Clear(const void* owner) {
  std::scoped_lock lock(lock_);
  entries_.erase(owner);
  count_.store(static_cast<int>(entries_.size()), std::memory_order_relaxed);
}

std::vector<std::string> BusyTracker::Commands() const {
  std::vector<const Entry*> sorted;
  std::vector<std::string> commands;
  std::scoped_lock lock(lock_);
  sorted.reserve(entries_.size());
  for (const auto& [owner, entry] : entries_)
    sorted.push_back(&entry);
  std::sort(sorted.begin(), sorted.end(),
            [](const Entry* a, const Entry* b) { return a->order < b->order; });
  commands.reserve(sorted.size());
  for (const Entry* entry : sorted)
    commands.push_back(entry->command);
  return commands;
}
