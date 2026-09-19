// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_DOUBLE_BUFFER_H
#define BASE_DOUBLE_BUFFER_H

#include <algorithm>
#include <mutex>
#include <span>
#include <tuple>
#include <vector>

namespace double_buffer_internal {

template <typename T>
struct Slot {
  std::vector<T> buffer[2];
  size_t size[2] = {};

  void MergeClear() { size[0] = 0; }

  void MergeAppend() {
    if (size[1] > 0) {
      size_t new_size = size[0] + size[1];
      if (new_size > buffer[0].size())
        buffer[0].resize(new_size);
      std::move(buffer[1].begin(), buffer[1].begin() + size[1],
                buffer[0].begin() + size[0]);
      size[0] = new_size;
      size[1] = 0;
    }
  }

  void Reset() { size[1] = 0; }

  void Push(T item) {
    if (size[1] < buffer[1].size())
      buffer[1][size[1]] = std::move(item);
    else
      buffer[1].push_back(std::move(item));
    ++size[1];
  }

  std::span<const T> data() const { return {buffer[0].data(), size[0]}; }
};

}  // namespace double_buffer_internal

// Thread-safe double buffer for exchanging data between a worker thread and
// the main thread. [0] is read by the main thread, [1] is written by the
// worker. On each frame, Merge() locks and moves [1] into [0].
//
// Backing storage is never deallocated and elements are never destroyed on
// clear. Instead, a logical size tracks the active range. Old elements beyond
// the logical size keep their allocations and are overwritten (via move-assign)
// when new data arrives, amortizing destruction cost across frames.
//
// Multiple types can be specified to bundle several buffers under one lock.
template <typename... Ts>
class DoubleBuffer {
 public:
  // Main thread: merge [1] into [0]. Returns true if [0] was cleared.
  bool Merge() {
    bool did_clear = false;
    std::scoped_lock scoped_lock(lock_);
    if (clear_in_main_thread_) {
      clear_in_main_thread_ = false;
      did_clear = true;
      std::apply([](auto&... s) { (s.MergeClear(), ...); }, slots_);
    }
    std::apply([](auto&... s) { (s.MergeAppend(), ...); }, slots_);
    return did_clear;
  }

  // Worker thread: append a single item to the buffer for type T.
  template <typename T = std::tuple_element_t<0, std::tuple<Ts...>>>
  void Push(T item) {
    std::scoped_lock scoped_lock(lock_);
    std::get<double_buffer_internal::Slot<T>>(slots_).Push(std::move(item));
  }

  // Worker thread: clear [1] and schedule [0] to be cleared on next Merge().
  void Reset() {
    std::scoped_lock scoped_lock(lock_);
    std::apply([](auto&... s) { (s.Reset(), ...); }, slots_);
    clear_in_main_thread_ = true;
  }

  // Main thread: read-only access to [0] for the buffer of type T.
  template <typename T = std::tuple_element_t<0, std::tuple<Ts...>>>
  std::span<const T> data() const {
    return std::get<double_buffer_internal::Slot<T>>(slots_).data();
  }

 private:
  std::tuple<double_buffer_internal::Slot<Ts>...> slots_;
  bool clear_in_main_thread_ = false;
  std::mutex lock_;  // Protects buffer[1], size[1], and clear_in_main_thread_.
};

#endif  // BASE_DOUBLE_BUFFER_H
