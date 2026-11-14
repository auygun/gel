// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_BUSY_TRACKER_H
#define GEL_COMMANDS_BUSY_TRACKER_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// The git commands running in the background, for the toolbar's busy spinner
// and its tooltip. Git workers report their current command from their worker
// threads; the main thread reads the list once per frame.
class BusyTracker {
 public:
  static BusyTracker& Get();

  BusyTracker(BusyTracker const&) = delete;
  BusyTracker& operator=(BusyTracker const&) = delete;

  // Worker thread: |owner| runs |command|, replacing what it ran before.
  void Set(const void* owner, std::string command);

  // Worker thread: |owner| no longer runs anything.
  void Clear(const void* owner);

  // True while any command runs. Cheap enough to call every frame.
  bool busy() const { return count_.load(std::memory_order_relaxed) > 0; }

  // Main thread: the running commands, in the order they started.
  std::vector<std::string> Commands() const;

 private:
  BusyTracker() = default;

  struct Entry {
    uint64_t order = 0;
    std::string command;
  };

  mutable std::mutex lock_;
  std::map<const void*, Entry> entries_;  // Guarded by lock_.
  uint64_t next_order_ = 0;               // Guarded by lock_.
  std::atomic<int> count_{0};
};

#endif  // GEL_COMMANDS_BUSY_TRACKER_H
