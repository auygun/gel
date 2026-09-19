// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/pipe.h"

#if !defined(OS_WIN)
#include <fcntl.h>
#include <unistd.h>
#endif

Pipe::Pipe() = default;

Pipe::~Pipe() {
  CloseAll();
}

Pipe::Pipe(Pipe&& other) noexcept {
#if defined(OS_WIN)
  handles_[0] = other.handles_[0];
  handles_[1] = other.handles_[1];
  other.handles_[0] = other.handles_[1] = INVALID_HANDLE_VALUE;
#else
  fd_[0] = other.fd_[0];
  fd_[1] = other.fd_[1];
  other.fd_[0] = other.fd_[1] = -1;
#endif
}

Pipe& Pipe::operator=(Pipe&& other) noexcept {
  CloseAll();
#if defined(OS_WIN)
  handles_[0] = other.handles_[0];
  handles_[1] = other.handles_[1];
  other.handles_[0] = other.handles_[1] = INVALID_HANDLE_VALUE;
#else
  fd_[0] = other.fd_[0];
  fd_[1] = other.fd_[1];
  other.fd_[0] = other.fd_[1] = -1;
#endif
  return *this;
}

bool Pipe::Create() {
#if defined(OS_WIN)
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  return CreatePipe(&handles_[kRead], &handles_[kWrite], &sa, 0) != 0;
#else
  if (pipe(fd_))
    return false;
  return true;
#endif
}

void Pipe::CloseAll() {
  Close(kRead);
  Close(kWrite);
}

void Pipe::Close(int which) {
#if defined(OS_WIN)
  if (handles_[which] != INVALID_HANDLE_VALUE) {
    CloseHandle(handles_[which]);
    handles_[which] = INVALID_HANDLE_VALUE;
  }
#else
  if (fd_[which] >= 0) {
    close(fd_[which]);
    fd_[which] = -1;
  }
#endif
}

bool Pipe::MakeNonBlocking(int which) {
#if defined(OS_WIN)
  // Windows anonymous pipes don't support non-blocking mode directly.
  // Exec::Poll uses PeekNamedPipe for non-blocking reads instead.
  return handles_[which] != INVALID_HANDLE_VALUE;
#else
  if (fd_[which] < 0)
    return false;
  auto flags = fcntl(fd_[which], F_GETFL, 0);
  if (flags < 0)
    return false;
  if (flags & O_NONBLOCK)
    return true;
  return fcntl(fd_[which], F_SETFL, flags | O_NONBLOCK) != 0;
#endif
}
