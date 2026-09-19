// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_EXEC_H
#define BASE_EXEC_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/pipe.h"

class Exec {
 public:
  enum class Status { UNINITIALIZED, RUNNING, EXITED, KILLED, SYSTEM_ERROR };

  Exec() = default;

#if defined(OS_WIN)
  ~Exec();
  Exec(Exec&& other) noexcept;
  Exec& operator=(Exec&& other) noexcept;
#else
  ~Exec() = default;
  Exec(Exec&& other) noexcept = default;
  Exec& operator=(Exec&& other) noexcept = default;
#endif

  bool Start(const std::vector<std::string>& args, bool with_stdin = false);

  bool Poll();

  bool Kill(bool terminate = false);

  // Detach the child process, allowing it to continue running independently.
  // Closes all pipes and releases process handles without killing or waiting.
  void Detach();

  // Write data to the child's stdin. Only valid after Start() with
  // with_stdin=true and before CloseStdin(). Returns the number of bytes
  // written, which may be less than data.size() if the pipe buffer is full.
  // Returns 0 if the pipe would block, -1 on error.
  int64_t Write(std::span<const char> data);

  // Close the stdin pipe, signalling EOF to the child process.
  void CloseStdin();

  Status GetStatus() const { return status_; }

  int GetResult() const { return result_; }

  std::string& GetOut() { return out_; }
  std::string& GetErr() { return err_; }

  int pid() const { return pid_; }

 private:
  Status status_ = Status::UNINITIALIZED;
  int result_ = 0;
  int pid_ = 0;
#if defined(OS_WIN)
  HANDLE process_handle_ = INVALID_HANDLE_VALUE;
  HANDLE thread_handle_ = INVALID_HANDLE_VALUE;
  bool killed_ = false;
#endif
  Pipe in_pipe_;
  Pipe out_pipe_;
  Pipe err_pipe_;
  std::string out_;
  std::string err_;
};

#endif  // BASE_EXEC_H
