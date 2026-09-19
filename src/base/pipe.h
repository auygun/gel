// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_PIPE_H
#define BASE_PIPE_H

#if defined(OS_WIN)
#include <windows.h>
#endif

class Pipe {
 public:
  enum Direction { kRead = 0, kWrite };

  Pipe();
  ~Pipe();

  Pipe(Pipe&& other) noexcept;
  Pipe& operator=(Pipe&& other) noexcept;

  bool Create();

#if defined(OS_WIN)
  HANDLE operator[](int which) const { return handles_[which]; }
  operator bool() const {
    return handles_[kRead] != INVALID_HANDLE_VALUE ||
           handles_[kWrite] != INVALID_HANDLE_VALUE;
  }
#else
  int operator[](int which) const { return fd_[which]; }
  operator bool() const { return fd_[kRead] >= 0 || fd_[kWrite] >= 0; }
#endif

  void CloseAll();
  void Close(int which);

  bool MakeNonBlocking(int which);

 private:
#if defined(OS_WIN)
  HANDLE handles_[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
#else
  int fd_[2] = {-1, -1};
#endif
};

#endif  // BASE_PIPE_H
