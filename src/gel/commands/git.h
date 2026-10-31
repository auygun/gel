// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_H
#define GEL_COMMANDS_GIT_H

#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
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

  // Line splitting state of one output stream, kept between Poll cycles.
  struct LineSplitter {
    // Incomplete line.
    std::string carryover;
    // Last progress line if the previous chunk ended with '\r'. It becomes a
    // regular line if the next chunk starts with '\n'.
    std::optional<std::string> cr_line;
  };
  LineSplitter out_splitter_;
  LineSplitter err_splitter_;

  void WorkerMain();

  bool Poll(Exec& proc);

  // Splits |chunk| into lines, prepending the splitter's carryover and keeping
  // an incomplete last line in it while |more| output is expected. Calls
  // |on_line(line, is_progress)| for each complete line. If |split_on_cr| is
  // set, a lone '\r' terminates a progress line.
  template <typename OnLine>
  static void SplitLines(LineSplitter& splitter,
                         std::string& chunk,
                         bool more,
                         bool split_on_cr,
                         OnLine on_line);

  // Called from the worker thread.
  virtual void OnStarted() = 0;
  virtual void OnOutput(std::string line) = 0;
  virtual void OnError(std::string line) = 0;
  // Called for a stderr line terminated by a lone '\r' (e.g. "Rebasing
  // (1/3)\r"), which the next line is meant to overwrite.
  virtual void OnProgress(std::string line) {}
  virtual void OnFinished(Exec::Status, int result) = 0;
  virtual void OnKilled() = 0;
};

#endif  // GEL_COMMANDS_GIT_H
