// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_CAT_FILE_H
#define GEL_COMMANDS_GIT_CAT_FILE_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "gel/commands/git.h"

// Runs "git cat-file --batch-check=%(objectsize)" to query blob sizes.
// Blob hashes are written to stdin; one size per line is read from stdout.
// Results are delivered to the main thread via Update().
class GitCatFile final : public Git {
 public:
  explicit GitCatFile(std::function<void(bool)> busy_callback);
  ~GitCatFile() final;

  GitCatFile(GitCatFile const&) = delete;
  GitCatFile& operator=(GitCatFile const&) = delete;

  // Starts a background query for the sizes of the given blob hashes.
  bool Run(const std::vector<std::string>& hashes);

  // Main thread: returns true if new results are available since the last
  // call, filling |sizes| with one size per input hash (in order).
  bool Update(std::vector<int64_t>& sizes);

 private:
  // Worker-thread state for accumulating output.
  std::vector<int64_t> sizes_;

  // Result passed from worker to main thread.
  std::mutex result_lock_;
  bool has_result_ = false;
  std::vector<int64_t> result_;

  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final;
};

#endif  // GEL_COMMANDS_GIT_CAT_FILE_H
