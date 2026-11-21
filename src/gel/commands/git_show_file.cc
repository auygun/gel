// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_show_file.h"

GitShowFile::GitShowFile(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitShowFile::~GitShowFile() {
  TerminateWorkerThread();
}

void GitShowFile::Run(const std::string& blob,
                      const std::filesystem::path& toplevel) {
  int run = ++last_run_;
  valid_ = false;
  done_ = false;
  failed_ = false;
  std::vector<std::string> args;
  if (!toplevel.empty())
    args = {"-C", toplevel.string()};
  args.insert(args.end(), {"cat-file", "blob", blob});
  if (!RunWithArgs(std::move(args), {}, run)) {
    done_ = true;
    failed_ = true;
  }
}

void GitShowFile::Stop() {
  ++last_run_;
  valid_ = false;
  done_ = true;
  failed_ = false;
  Kill();
}

void GitShowFile::Update() {
  // Read before merging: a run's lines are all pushed before it reports
  // finishing, and its buffer reset before it reports starting, so the merge
  // below takes them in.
  int started = started_run_.load(std::memory_order_acquire);
  int finished = finished_run_.load(std::memory_order_acquire);
  bool failed = finished_failed_.load(std::memory_order_relaxed);
  buffer_.Merge();
  if (started == last_run_)
    valid_ = true;
  if (finished == last_run_) {
    done_ = true;
    failed_ = failed;
  }
}

void GitShowFile::OnStarted() {
  buffer_.Reset();
  started_run_.store(run_id(), std::memory_order_release);
}

void GitShowFile::OnOutput(std::string line) {
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  buffer_.Push(std::move(line));
}

void GitShowFile::OnFinished(Exec::Status status, int result) {
  finished_failed_.store(status != Exec::Status::EXITED || result != 0,
                         std::memory_order_relaxed);
  finished_run_.store(run_id(), std::memory_order_release);
}
