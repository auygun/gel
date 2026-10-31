// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_log.h"

#include "third_party/kaliber/base/log.h"

using namespace base;

namespace {

// Field indices in the --pretty=tformat output. Each field is separated by
// \x01 (SOH). The format string in the constructor must match this order.
// To add a field: append a %x01<placeholder> to the format string, add an
// entry here before kFieldCount, and assign it in OnOutput.
enum Field : size_t {
  kCommit,
  kParents,
  kAuthor,
  kAuthorDate,
  kCommitter,
  kCommitterDate,
  kSubject,
  kBody,
  kFieldCount,
};

}  // namespace

GitLog::GitLog(std::function<void(bool)> busy_callback)
    : Git(std::move(busy_callback)) {}

GitLog::~GitLog() {
  TerminateWorkerThread();
}

void GitLog::SetExtraArgs(std::vector<std::string> args) {
  extra_args_ = std::move(args);
}

void GitLog::SetPathFilter(std::vector<std::string> args) {
  path_filter_ = std::move(args);
}

bool GitLog::Run() {
  std::vector<std::string> args = {
      // No %d: decorating makes git read every ref's object before printing
      // the first commit. RefDecorations loads the labels instead.
      "log", "--parents", "--date=format-local:%Y-%m-%d %H:%M:%S",
      "--pretty=tformat:"
      "%H%x01%P%x01%aN <%aE>%x01%ad%x01%cN <%cE>%x01%cd%x01%s%x01%b"};
  args.insert(args.end(), extra_args_.begin(), extra_args_.end());
  if (!path_filter_.empty()) {
    args.emplace_back("--");
    args.insert(args.end(), path_filter_.begin(), path_filter_.end());
  }
  return RunWithArgs(std::move(args));
}

void GitLog::Update() {
  did_clear_ = commit_history_.Merge();
}

void GitLog::FlushPendingCommit() {
  if (pending_commit_) {
    commit_history_.Push(std::move(*pending_commit_));
    pending_commit_.reset();
  }
}

void GitLog::OnStarted() {
  pending_commit_.reset();
  commit_history_.Reset();
  {
    std::scoped_lock lock(error_lock_);
    has_info_message_ = false;
    info_message_.clear();
  }
}

void GitLog::OnOutput(std::string line) {
  // Split on \x01 delimiter.
  fields_.clear();
  size_t start = 0;
  for (;;) {
    size_t pos = line.find('\x01', start);
    if (pos == std::string::npos) {
      fields_.push_back(line.substr(start));
      break;
    }
    fields_.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }

  // Lines with fewer fields than expected are body continuation lines
  // from %b (which contains embedded newlines).
  if (fields_.size() < kFieldCount) {
    if (pending_commit_)
      pending_commit_->message.push_back(std::move(line));
    return;
  }

  FlushPendingCommit();

  CommitInfo info;
  info.commit = std::move(fields_[kCommit]);

  std::string& parents = fields_[kParents];
  size_t pstart = 0;
  while (pstart < parents.size()) {
    size_t end = parents.find(' ', pstart);
    if (end == std::string::npos)
      end = parents.size();
    info.parents.push_back(parents.substr(pstart, end - pstart));
    pstart = end + 1;
  }

  info.author = std::move(fields_[kAuthor]);
  info.author_date = std::move(fields_[kAuthorDate]);
  info.committer = std::move(fields_[kCommitter]);
  info.committer_date = std::move(fields_[kCommitterDate]);

  info.message.push_back(std::move(fields_[kSubject]));

  // The first line of the body (from %b) is in the same record line.
  // Subsequent body lines arrive as separate OnOutput calls (handled above).
  if (!fields_[kBody].empty())
    info.message.push_back(std::move(fields_[kBody]));

  pending_commit_ = std::move(info);
}

bool GitLog::GetError(std::string& error) {
  std::scoped_lock lock(error_lock_);
  if (!has_error_)
    return false;
  error = std::move(error_);
  has_error_ = false;
  return true;
}

bool GitLog::GetInfoMessage(std::string& message) {
  std::scoped_lock lock(error_lock_);
  if (!has_info_message_)
    return false;
  message = std::move(info_message_);
  has_info_message_ = false;
  return true;
}

void GitLog::OnFinished(Exec::Status status, int result, std::string err) {
  FlushPendingCommit();
  if (status == Exec::Status::KILLED)
    return;
  if (status != Exec::Status::EXITED || result != 0) {
    std::scoped_lock lock(error_lock_);
    if (err.find("does not have any commits") != std::string::npos) {
      info_message_ = std::move(err);
      has_info_message_ = true;
    } else {
      error_ = std::move(err);
      has_error_ = true;
    }
  }
}

void GitLog::OnKilled() {
  pending_commit_.reset();
}
