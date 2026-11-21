// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_FILE_READER_H
#define BASE_FILE_READER_H

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>

#include "base/double_buffer.h"

// Reads a file's lines in the background, on the thread pool, and hands them
// to the main thread through a double buffer. Lines are split on '\n', and a
// trailing '\r' is dropped. A new Read() supersedes the one in progress, which
// stops at its next line.
class FileReader {
 public:
  FileReader();
  ~FileReader();

  FileReader(FileReader const&) = delete;
  FileReader& operator=(FileReader const&) = delete;

  // Main thread: starts reading |path|. The lines of the previous read are
  // dropped right away.
  void Read(const std::filesystem::path& path);

  // Main thread: stops reading and drops the lines.
  void Stop();

  // Main thread: merges the lines read since the last call.
  void Update();

  // Main thread: the lines of the latest read merged so far.
  std::span<const std::string> lines() const { return shared_->buffer.data(); }

  // Main thread: true once all lines of the latest read are merged, or it was
  // stopped. Valid after Update().
  bool done() const { return done_; }

  // Main thread: true if the latest read is done and the file could not be
  // opened or read to the end. Valid after Update().
  bool failed() const { return failed_; }

 private:
  // State shared with the thread pool's reads, which can outlive this object.
  struct Shared {
    std::mutex lock;
    // Incremented by each read. A read only adds to the buffer while it is
    // the latest. Guarded by |lock|.
    int generation = 0;
    // The latest read that pushed all its lines, and whether it failed.
    // Guarded by |lock|.
    int finished = 0;
    bool failed = false;
    DoubleBuffer<std::string> buffer;
  };

  // Starts a new read and returns its generation.
  int NextGeneration();

  std::shared_ptr<Shared> shared_;
  bool done_ = true;
  bool failed_ = false;
};

#endif  // BASE_FILE_READER_H
