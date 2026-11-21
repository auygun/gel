// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/file_reader.h"

#include <fstream>

#include "third_party/kaliber/base/thread_pool.h"

FileReader::FileReader() : shared_(std::make_shared<Shared>()) {}

FileReader::~FileReader() {
  // A read in progress stops at its next line.
  Stop();
}

int FileReader::NextGeneration() {
  std::scoped_lock lock(shared_->lock);
  // Drop what the previous read showed right away, along with what it pushed
  // but wasn't merged yet. Reads only push holding |lock|, so none can push in
  // between.
  shared_->buffer.Reset();
  shared_->buffer.Clear();
  return ++shared_->generation;
}

void FileReader::Read(const std::filesystem::path& path) {
  int generation = NextGeneration();
  done_ = false;
  failed_ = false;
  base::ThreadPool::Get().PostTask(
      HERE, [shared = shared_, generation, path]() {
        bool failed = true;
        std::ifstream in(path, std::ios::binary);
        if (in) {
          std::string line;
          bool superseded = false;
          while (!superseded && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
              line.pop_back();
            std::scoped_lock lock(shared->lock);
            superseded = generation != shared->generation;
            if (!superseded)
              shared->buffer.Push(std::move(line));
          }
          failed = in.bad();
        }
        std::scoped_lock lock(shared->lock);
        if (generation == shared->generation) {
          shared->finished = generation;
          shared->failed = failed;
        }
      });
}

void FileReader::Stop() {
  int generation = NextGeneration();
  std::scoped_lock lock(shared_->lock);
  shared_->finished = generation;
  shared_->failed = false;
  done_ = true;
  failed_ = false;
}

void FileReader::Update() {
  // Read before merging: a read pushes all its lines before it reports
  // finishing, so the merge below takes them in.
  bool finished;
  bool failed;
  {
    std::scoped_lock lock(shared_->lock);
    finished = shared_->finished == shared_->generation;
    failed = shared_->failed;
  }
  shared_->buffer.Merge();
  if (finished) {
    done_ = true;
    failed_ = failed;
  }
}
