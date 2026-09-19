// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git.h"

#include <algorithm>

#include "third_party/kaliber/base/log.h"

using namespace base;

Git::Git(std::function<void(bool)> busy_callback)
    : busy_callback_(std::move(busy_callback)),
      worker_(std::thread(&Git::WorkerMain, this)) {}

Git::~Git() {
  DCHECK(!worker_.joinable())
      << "Did you forget to call TerminateWorkerThread() from derived class?";
}

void Git::TerminateWorkerThread() {
  if (worker_.joinable()) {
    quit_.store(true, std::memory_order_relaxed);
    semaphore_.release();
    worker_.join();
  }
}

bool Git::RunWithArgs(std::vector<std::string> args, std::string stdin_data) {
  // Start a new git process and hand it to the worker thread.
  args.insert(args.begin(), "git");
  Exec proc;
  if (!proc.Start(args, !stdin_data.empty()))
    return false;
  {
    std::scoped_lock scoped_lock(lock_);
    new_procs_.push_front(std::move(proc));
    pending_stdin_ = std::move(stdin_data);
    detach_requested_ = false;
  }
  semaphore_.release();
  return true;
}

void Git::Kill() {
  // Pass an uninitialized Exec to the worker thread to kill the current
  // process.
  {
    std::scoped_lock scoped_lock(lock_);
    new_procs_.emplace_front();
    detach_requested_ = false;
  }
  semaphore_.release();
}

void Git::Detach() {
  {
    std::scoped_lock scoped_lock(lock_);
    detach_requested_ = true;
    new_procs_.emplace_front();
  }
  semaphore_.release();
}

void Git::WorkerMain() {
  for (;;) {
    if (quit_.load(std::memory_order_relaxed))
      break;

    semaphore_.acquire();
    busy_.store(true, std::memory_order_relaxed);
    busy_callback_(true);

    do {
      if (quit_.load(std::memory_order_relaxed))
        break;

      // Pick up new processes queued by the main thread.
      std::list<Exec> procs;
      std::string stdin_data;
      bool detach = false;
      {
        std::scoped_lock scoped_lock(lock_);
        procs.swap(new_procs_);
        stdin_data = std::move(pending_stdin_);
        detach = detach_requested_;
        detach_requested_ = false;
      }

      if (!procs.empty()) {
        bool any_killed = false;

        // Kill or detach the current process.
        if (curent_proc_.GetStatus() == Exec::Status::RUNNING) {
          if (detach) {
            DLOG(0) << "Detached - pid: " << curent_proc_.pid();
            curent_proc_.Detach();
            curent_proc_ = {};
            detached_.store(true, std::memory_order_relaxed);
          } else {
            curent_proc_.Kill();
            any_killed = true;
            DLOG(0) << "Killed - pid: " << curent_proc_.pid();
            death_row_.push_back({std::move(curent_proc_), {}});
          }
        }

        // Take the latest process as the new current process.
        curent_proc_ = std::move(*procs.begin());
        procs.pop_front();

        // Kill any older processes and move them to death_row_.
        for (auto& proc : procs) {
          if (proc.GetStatus() == Exec::Status::RUNNING) {
            proc.Kill();
            any_killed = true;
            DLOG(0) << "Killed - pid: " << proc.pid();
          }
        }
        for (auto& proc : procs)
          death_row_.push_back({std::move(proc), {}});

        if (any_killed)
          OnKilled();
        if (curent_proc_.GetStatus() == Exec::Status::RUNNING) {
          current_stdin_ = std::move(stdin_data);
          stdin_offset_ = 0;
          stdin_open_ = !current_stdin_.empty();
          carryover_.clear();
          OnStarted();
          DLOG(0) << "Started - pid: " << curent_proc_.pid();
        }
      }

      // Poll the current process and clear it when terminated.
      if (curent_proc_.GetStatus() != Exec::Status::UNINITIALIZED &&
          !Poll(curent_proc_)) {
        DLOG(0) << "Terminated - pid: " << curent_proc_.pid();
        curent_proc_ = {};
      }

      // Poll killed processes and remove them once terminated.
      // Escalate to SIGKILL after a grace period.
      for (auto it = death_row_.begin(); it != death_row_.end();) {
        if (it->proc.GetStatus() != Exec::Status::UNINITIALIZED &&
            Poll(it->proc)) {
          if (it->kill_timer.Elapsed() > 10.0) {
            it->proc.Kill(true);
            Poll(it->proc);
            DLOG(0) << "Terminated (SIGKILL) - pid: " << it->proc.pid();
            it = death_row_.erase(it);
          } else {
            ++it;
          }
        } else {
          DLOG(0) << "Terminated (SIGINT) - pid: " << it->proc.pid();
          it = death_row_.erase(it);
        }
      }
    } while (curent_proc_.GetStatus() != Exec::Status::UNINITIALIZED ||
             !death_row_.empty());
    busy_.store(false, std::memory_order_relaxed);
    busy_callback_(false);
  }
}

bool Git::Poll(Exec& proc) {
  DCHECK(std::this_thread::get_id() == worker_.get_id());

  bool more = proc.Poll();

  // Write pending stdin data to the current process.
  if (stdin_open_ && proc.pid() == curent_proc_.pid()) {
    if (stdin_offset_ < current_stdin_.size()) {
      auto written = proc.Write({current_stdin_.data() + stdin_offset_,
                                 current_stdin_.size() - stdin_offset_});
      if (written > 0)
        stdin_offset_ += static_cast<size_t>(written);
      else if (written < 0)
        stdin_open_ = false;
    } else {
      proc.CloseStdin();
      stdin_open_ = false;
      current_stdin_.clear();
    }
  }

  // Extract output and clear the buffer to prevent unbounded memory growth.
  std::string chunk;
  chunk.swap(proc.GetOut());

  if (proc.pid() == curent_proc_.pid() && !chunk.empty()) {
    if (!carryover_.empty()) {
      chunk.insert(0, carryover_);
      carryover_.clear();
    }

    size_t start = 0;
    for (;;) {
      auto nl = chunk.find('\n', start);
      if (nl == std::string::npos) {
        if (more && start < chunk.size())
          carryover_ = chunk.substr(start);
        break;
      }
      OnOutput(chunk.substr(start, nl - start));
      start = nl + 1;
    }
  }

  if (!more && proc.pid() == curent_proc_.pid()) {
    DLOG(0) << "Finished -  pid: " << curent_proc_.pid()
            << ", status: " << static_cast<int>(curent_proc_.GetStatus())
            << ", result: " << curent_proc_.GetResult()
            << ", err: " << curent_proc_.GetErr();
    OnFinished(proc.GetStatus(), proc.GetResult(), std::move(proc.GetErr()));
  }

  return more;
}
