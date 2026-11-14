// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/exec.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>

#include "third_party/kaliber/base/log.h"

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

  pid_ = fork();
  if (pid_ < 0) {
    DLOG(0) << "fork() failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }
  if (pid_ == 0) {
    // Child
    if (with_stdin) {
      if (dup2(in_pipe_[Pipe::kRead], STDIN_FILENO) < 0)
        _exit(-1);
      in_pipe_.CloseAll();
    } else {
      close(STDIN_FILENO);
    }
    if (dup2(out_pipe_[Pipe::kWrite], STDOUT_FILENO) < 0 ||
        dup2(err_pipe_[Pipe::kWrite], STDERR_FILENO) < 0)
      _exit(-1);
    out_pipe_.CloseAll();
    err_pipe_.CloseAll();

    std::vector<const char*> pargs;
    pargs.reserve(args.size());
    for (auto const& arg : args) {
      pargs.push_back(arg.c_str());
    }
    pargs.push_back(nullptr);
    execvp(pargs.front(), const_cast<char* const*>(pargs.data()));
    _exit(-1);
  } else {
    status_ = Status::RUNNING;
    if (with_stdin) {
      in_pipe_.Close(Pipe::kRead);
      in_pipe_.MakeNonBlocking(Pipe::kWrite);
#if defined(F_SETNOSIGPIPE)
      // See Write(). macOS can turn SIGPIPE off for the descriptor itself.
      fcntl(in_pipe_[Pipe::kWrite], F_SETNOSIGPIPE, 1);
#endif
    }
    out_pipe_.Close(Pipe::kWrite);
    out_pipe_.MakeNonBlocking(Pipe::kRead);
    err_pipe_.Close(Pipe::kWrite);
    err_pipe_.MakeNonBlocking(Pipe::kRead);
  }
  return true;
}

bool Exec::Poll() {
  DCHECK(status_ != Status::UNINITIALIZED);

  struct pollfd fd[2];
  size_t fds = 0;
  char buffer[32768];

  if (out_pipe_) {
    fd[fds].fd = out_pipe_[Pipe::kRead];
    fd[fds].events = POLL_IN;
    ++fds;
  }
  if (err_pipe_) {
    fd[fds].fd = err_pipe_[Pipe::kRead];
    fd[fds].events = POLL_IN;
    ++fds;
  }
  if (fds > 0 && poll(fd, fds, 1) < 0) {
    DLOG(0) << "poll failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }

  if (out_pipe_) {
    auto bytes_read = read(out_pipe_[Pipe::kRead], buffer, sizeof(buffer));
    if (bytes_read == 0) {
      out_pipe_.Close(Pipe::kRead);
    } else if (bytes_read > 0) {
      out_.append(buffer, bytes_read);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      DLOG(0) << "read out_pipe failed";
      status_ = Status::SYSTEM_ERROR;
      return false;
    }
  }

  if (err_pipe_) {
    auto bytes_read = read(err_pipe_[Pipe::kRead], buffer, sizeof(buffer));
    if (bytes_read == 0) {
      err_pipe_.Close(Pipe::kRead);
    } else if (bytes_read > 0) {
      err_.append(buffer, bytes_read);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      DLOG(0) << "read err_pipe failed";
      status_ = Status::SYSTEM_ERROR;
      return false;
    }
  }

  if (status_ == Status::RUNNING) {
    int proc_status = 0;
    pid_t ret = waitpid(pid_, &proc_status, WNOHANG);
    if (ret < 0) {
      DLOG(0) << "waitpid failed";
      status_ = Status::SYSTEM_ERROR;
      return false;
    }
    if (ret != 0) {
      if (WIFEXITED(proc_status)) {
        status_ = Status::EXITED;
        result_ = WEXITSTATUS(proc_status);
      } else if (WIFSIGNALED(proc_status) && WTERMSIG(proc_status) == SIGINT) {
        status_ = Status::KILLED;
      } else {
        DLOG(0) << "exec_status: " << proc_status;
        status_ = Status::SYSTEM_ERROR;
        return false;
      }
    }
  }

  return out_pipe_ || err_pipe_ || status_ == Status::RUNNING;
}

bool Exec::Kill(bool terminate) {
  DCHECK(status_ != Status::UNINITIALIZED);

  if (kill(pid_, terminate ? SIGKILL : SIGINT)) {
    DLOG(0) << "kill failed";
    status_ = Status::SYSTEM_ERROR;
    return false;
  }
  return true;
}

void Exec::Detach() {
  in_pipe_.CloseAll();
  out_pipe_.CloseAll();
  err_pipe_.CloseAll();
  out_.clear();
  err_.clear();
  status_ = Status::UNINITIALIZED;
  pid_ = 0;
}

int64_t Exec::Write(std::span<const char> data) {
  // A child that exits before reading all of its input (e.g. git rejecting an
  // invalid argument) makes the write raise SIGPIPE, which terminates the
  // whole process by default.
#if defined(F_SETNOSIGPIPE)
  // Turned off for the descriptor in Start().
  auto written = write(in_pipe_[Pipe::kWrite], data.data(), data.size());
#else
  // Block SIGPIPE on this thread for the write and discard it if it was
  // raised, leaving the process-wide disposition alone.
  sigset_t sigpipe_set, old_set;
  sigemptyset(&sigpipe_set);
  sigaddset(&sigpipe_set, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &sigpipe_set, &old_set);
  auto written = write(in_pipe_[Pipe::kWrite], data.data(), data.size());
  int write_errno = errno;
  if (written < 0 && write_errno == EPIPE) {
    // Consume the SIGPIPE the failed write left pending on this thread.
    timespec zero = {};
    sigtimedwait(&sigpipe_set, nullptr, &zero);
  }
  pthread_sigmask(SIG_SETMASK, &old_set, nullptr);
  errno = write_errno;
#endif
  if (written < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    return -1;
  }
  return written;
}
