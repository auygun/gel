// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_COMMANDS_GIT_BLAME_H
#define GEL_COMMANDS_GIT_BLAME_H

#include <filesystem>
#include <mutex>
#include <string>

#include "gel/commands/git.h"

// Runs "git blame --porcelain" in the background and parses the result.
class GitBlame final : public Git {
 public:
  struct Result {
    bool success = false;
    std::string hash;
    std::string file;
    int line = 0;
  };

  explicit GitBlame(std::function<void(bool)> busy_callback);
  ~GitBlame() final;

  GitBlame(GitBlame const&) = delete;
  GitBlame& operator=(GitBlame const&) = delete;

  // Runs "git blame --porcelain -L<line>,<line> <revision> -- <file>".
  // When |toplevel| is non-empty, passes "-C <toplevel>" so that
  // repo-relative paths resolve correctly from any working directory.
  bool Run(const std::string& revision,
           const std::string& file,
           int line_number,
           const std::filesystem::path& toplevel = {});

  // Main thread: returns true if a blame completed since the last call,
  // filling |result| with the outcome.
  bool Update(Result& result);

 private:
  void OnStarted() final;
  void OnOutput(std::string line) final;
  void OnFinished(Exec::Status status, int result, std::string err) final;
  void OnKilled() final;

  std::mutex result_lock_;
  bool has_result_ = false;
  Result result_;

  // Worker-thread parsing state (no lock needed, only accessed from worker).
  bool first_line_ = false;
  std::string parse_hash_;
  std::string parse_file_;
  int parse_line_ = 0;
};

#endif  // GEL_COMMANDS_GIT_BLAME_H
