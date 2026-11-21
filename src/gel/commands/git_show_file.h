// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_SHOW_FILE_H
#define GEL_COMMANDS_GIT_SHOW_FILE_H

#include <atomic>
#include <filesystem>
#include <span>
#include <string>

#include "base/double_buffer.h"
#include "gel/commands/git.h"

// Runs "git cat-file blob" to load a blob's content, streamed to the main
// thread as lines. A trailing '\r' is dropped.
class GitShowFile final : public Git {
 public:
  explicit GitShowFile(std::function<void(bool)> busy_callback);
  ~GitShowFile() final;

  GitShowFile(GitShowFile const&) = delete;
  GitShowFile& operator=(GitShowFile const&) = delete;

  // Loads |blob|. Git runs in |toplevel| if it isn't empty. The lines of the
  // previous run are dropped right away.
  void Run(const std::string& blob, const std::filesystem::path& toplevel);

  // Stops loading and drops the lines.
  void Stop();

  // Main thread: merges the lines loaded since the last call.
  void Update();

  // Main thread: the lines of the latest run merged so far.
  std::span<const std::string> lines() const {
    return valid_ ? buffer_.data() : std::span<const std::string>();
  }

  // Main thread: true once all lines of the latest run are merged, or it
  // failed or was stopped. Valid after Update().
  bool done() const { return done_; }

  // Main thread: true if the latest run is done and git failed. Valid after
  // Update().
  bool failed() const { return failed_; }

 private:
  DoubleBuffer<std::string> buffer_;

  // Main thread: the latest run's id, and its state as of the last Update().
  // The buffer can still hold lines of an earlier run until the latest one
  // starts.
  int last_run_ = 0;
  bool valid_ = false;
  bool done_ = true;
  bool failed_ = false;
  // Worker thread: the run that started, and finished, last, and whether
  // that one failed.
  std::atomic<int> started_run_{0};
  std::atomic<int> finished_run_{0};
  std::atomic<bool> finished_failed_{false};

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnError(std::string) final {}
  void OnFinished(Exec::Status status, int result) final;
  void OnKilled() final {}
};

#endif  // GEL_COMMANDS_GIT_SHOW_FILE_H
