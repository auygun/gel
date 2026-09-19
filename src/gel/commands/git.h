// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_H
#define GEL_COMMANDS_GIT_H

#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "base/exec.h"
#include "third_party/kaliber/base/thread_pool.h"
#include "third_party/kaliber/base/timer.h"

// Base class for running git commands in a background thread. Derived classes
// implement the virtual interface to parse the output.
class Git {
 public:
  explicit Git(std::function<void(bool)> busy_callback);
  virtual ~Git();

  Git(Git const&) = delete;
  Git& operator=(Git const&) = delete;

  // Kills the currently running process.
  void Kill();

  // Detaches the currently running process, allowing it to continue running
  // independently. The process output is no longer tracked.
  void Detach();

  // Returns true while a command is in progress.
  bool busy() const { return busy_.load(std::memory_order_relaxed); }

  // Returns true after the process has been detached.
  bool detached() const { return detached_.load(std::memory_order_relaxed); }

 protected:
  // Starts a new git process with the given arguments. Any currently running
  // process is killed and superseded. If |stdin_data| is non-empty, the
  // process is started with a stdin pipe and the data is written to it.
  bool RunWithArgs(std::vector<std::string> args, std::string stdin_data = {});

 protected:
  void TerminateWorkerThread();

 private:
  std::list<Exec> new_procs_;
  std::string pending_stdin_;  // Guarded by lock_.
  std::mutex lock_;

  Exec curent_proc_;

  struct KilledProc {
    Exec proc;
    base::ElapsedTimer kill_timer;
  };
  std::list<KilledProc> death_row_;

  std::counting_semaphore<> semaphore_{0};
  std::atomic<bool> busy_{false};
  std::atomic<bool> quit_{false};
  bool detach_requested_ = false;  // Guarded by lock_.
  std::atomic<bool> detached_{false};
  std::function<void(bool)> busy_callback_;
  std::thread worker_;

  // Worker-thread state for writing stdin data to the current process.
  std::string current_stdin_;
  size_t stdin_offset_ = 0;
  bool stdin_open_ = false;

  // Incomplete line carried over between Poll cycles.
  std::string carryover_;

  void WorkerMain();

  bool Poll(Exec& proc);

  // Called from the worker thread.
  virtual void OnStarted() = 0;
  virtual void OnOutput(std::string line) = 0;
  virtual void OnFinished(Exec::Status, int result, std::string err) = 0;
  virtual void OnKilled() = 0;
};

#endif  // GEL_COMMANDS_GIT_H
