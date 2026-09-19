// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/exec.h"

#include <algorithm>

#include "third_party/kaliber/base/log.h"

Exec::~Exec() {
  if (process_handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(process_handle_);
  if (thread_handle_ != INVALID_HANDLE_VALUE)
    CloseHandle(thread_handle_);
}

Exec::Exec(Exec&& other) noexcept
    : status_(other.status_),
      result_(other.result_),
      pid_(other.pid_),
      process_handle_(other.process_handle_),
      thread_handle_(other.thread_handle_),
      killed_(other.killed_),
      in_pipe_(std::move(other.in_pipe_)),
      out_pipe_(std::move(other.out_pipe_)),
      err_pipe_(std::move(other.err_pipe_)),
      out_(std::move(other.out_)),
      err_(std::move(other.err_)) {
  other.status_ = Status::UNINITIALIZED;
  other.result_ = 0;
  other.pid_ = 0;
  other.process_handle_ = INVALID_HANDLE_VALUE;
  other.thread_handle_ = INVALID_HANDLE_VALUE;
  other.killed_ = false;
}

Exec& Exec::operator=(Exec&& other) noexcept {
  if (this != &other) {
    if (process_handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(process_handle_);
    if (thread_handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(thread_handle_);
    status_ = other.status_;
    result_ = other.result_;
    pid_ = other.pid_;
    process_handle_ = other.process_handle_;
    thread_handle_ = other.thread_handle_;
    killed_ = other.killed_;
    in_pipe_ = std::move(other.in_pipe_);
    out_pipe_ = std::move(other.out_pipe_);
    err_pipe_ = std::move(other.err_pipe_);
    out_ = std::move(other.out_);
    err_ = std::move(other.err_);
    other.status_ = Status::UNINITIALIZED;
    other.result_ = 0;
    other.pid_ = 0;
    other.process_handle_ = INVALID_HANDLE_VALUE;
    other.thread_handle_ = INVALID_HANDLE_VALUE;
    other.killed_ = false;
  }
  return *this;
}

bool Exec::Start(const std::vector<std::string>& args, bool with_stdin) {
  DCHECK(status_ == Status::UNINITIALIZED);

  if (with_stdin && !in_pipe_.Create()) {
    DLOG(0) << "in_pipe failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }
  if (!out_pipe_.Create()) {
    DLOG(0) << "out_pipe failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }
  if (!err_pipe_.Create()) {
    DLOG(0) << "err_pipe failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }

  // Prevent the parent-side handles from being inherited by the child process.
  if (with_stdin)
    SetHandleInformation(in_pipe_[Pipe::kWrite], HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_pipe_[Pipe::kRead], HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_pipe_[Pipe::kRead], HANDLE_FLAG_INHERIT, 0);

  // Build the command line string with each argument quoted and escaped.
  std::string cmd_line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      cmd_line += ' ';
    cmd_line += '"';
    size_t num_backslashes = 0;
    for (char c : args[i]) {
      if (c == '\\') {
        ++num_backslashes;
      } else if (c == '"') {
        cmd_line.append(num_backslashes * 2 + 1, '\\');
        cmd_line += '"';
        num_backslashes = 0;
      } else {
        num_backslashes = 0;
      }
      cmd_line += c;
    }
    // Escape trailing backslashes so they don't escape the closing quote.
    cmd_line.append(num_backslashes, '\\');
    cmd_line += '"';
  }

  STARTUPINFOA si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = with_stdin ? in_pipe_[Pipe::kRead] : INVALID_HANDLE_VALUE;
  si.hStdOutput = out_pipe_[Pipe::kWrite];
  si.hStdError = err_pipe_[Pipe::kWrite];

  PROCESS_INFORMATION pi = {};
  if (!CreateProcessA(nullptr, cmd_line.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    DLOG(0) << "CreateProcess failed: " << GetLastError();
    status_ = Status::SYSTEM_ERROR;
    return false;
  }

  process_handle_ = pi.hProcess;
  thread_handle_ = pi.hThread;
  pid_ = static_cast<int>(pi.dwProcessId);
  status_ = Status::RUNNING;

  // Close the child-side handles since the child has inherited them.
  if (with_stdin)
    in_pipe_.Close(Pipe::kRead);
  out_pipe_.Close(Pipe::kWrite);
  err_pipe_.Close(Pipe::kWrite);

  return true;
}

bool Exec::Poll() {
  DCHECK(status_ != Status::UNINITIALIZED);

  char buffer[32768];

  if (out_pipe_) {
    DWORD available = 0;
    if (PeekNamedPipe(out_pipe_[Pipe::kRead], nullptr, 0, nullptr, &available,
                      nullptr)) {
      if (available > 0) {
        DWORD bytes_read = 0;
        DWORD to_read =
            (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
        if (ReadFile(out_pipe_[Pipe::kRead], buffer, to_read, &bytes_read,
                     nullptr) &&
            bytes_read > 0) {
          out_.append(buffer, bytes_read);
        }
      }
    } else {
      // Pipe broken (child closed its end).
      out_pipe_.Close(Pipe::kRead);
    }
  }

  if (err_pipe_) {
    DWORD available = 0;
    if (PeekNamedPipe(err_pipe_[Pipe::kRead], nullptr, 0, nullptr, &available,
                      nullptr)) {
      if (available > 0) {
        DWORD bytes_read = 0;
        DWORD to_read =
            (std::min)(available, static_cast<DWORD>(sizeof(buffer)));
        if (ReadFile(err_pipe_[Pipe::kRead], buffer, to_read, &bytes_read,
                     nullptr) &&
            bytes_read > 0) {
          err_.append(buffer, bytes_read);
        }
      }
    } else {
      err_pipe_.Close(Pipe::kRead);
    }
  }

  if (status_ == Status::RUNNING) {
    DWORD ret = WaitForSingleObject(process_handle_, 0);
    if (ret == WAIT_OBJECT_0) {
      if (killed_) {
        status_ = Status::KILLED;
      } else {
        DWORD exit_code = 0;
        GetExitCodeProcess(process_handle_, &exit_code);
        status_ = Status::EXITED;
        result_ = static_cast<int>(exit_code);
      }
    } else if (ret == WAIT_FAILED) {
      DLOG(0) << "WaitForSingleObject failed: " << GetLastError();
      status_ = Status::SYSTEM_ERROR;
      return false;
    }
  }

  bool more = out_pipe_ || err_pipe_ || status_ == Status::RUNNING;
  if (more)
    Sleep(1);
  return more;
}

bool Exec::Kill(bool /*terminate*/) {
  DCHECK(status_ != Status::UNINITIALIZED);

  if (!TerminateProcess(process_handle_, 1)) {
    DLOG(0) << "TerminateProcess failed: " << GetLastError();
    status_ = Status::SYSTEM_ERROR;
    return false;
  }
  killed_ = true;
  return true;
}

void Exec::Detach() {
  in_pipe_.CloseAll();
  out_pipe_.CloseAll();
  err_pipe_.CloseAll();
  out_.clear();
  err_.clear();
  if (process_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(process_handle_);
    process_handle_ = INVALID_HANDLE_VALUE;
  }
  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(thread_handle_);
    thread_handle_ = INVALID_HANDLE_VALUE;
  }
  status_ = Status::UNINITIALIZED;
  pid_ = 0;
  killed_ = false;
}

int64_t Exec::Write(std::span<const char> data) {
  DWORD written = 0;
  if (!WriteFile(in_pipe_[Pipe::kWrite], data.data(),
                 static_cast<DWORD>(data.size()), &written, nullptr))
    return -1;
  return static_cast<int64_t>(written);
}
