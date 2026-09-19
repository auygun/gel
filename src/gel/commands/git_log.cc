// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/commands/git_log.h"

#include <algorithm>

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
  kBranch,
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
      "log", "--parents", "--decorate=full",
      "--date=format-local:%Y-%m-%d %H:%M:%S",
      "--pretty=tformat:"
      "%H%x01%P%x01%aN <%aE>%x01%ad%x01%cN <%cE>%x01%cd%x01%d%x01%s%x01%b"};
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
    std::sort(pending_commit_->tags.begin(), pending_commit_->tags.end());
    std::sort(pending_commit_->branches.begin(),
              pending_commit_->branches.end(),
              [](const BranchInfo& a, const BranchInfo& b) {
                return a.name < b.name;
              });
    commit_history_.Push(std::move(*pending_commit_));
    pending_commit_.reset();
  }
}

void GitLog::OnStarted() {
  pending_commit_.reset();
  commit_history_.Reset();
  {
    std::scoped_lock lock(head_branch_lock_);
    head_branch_.clear();
  }
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

  // %d includes a leading space before the opening parenthesis.
  // Strip "HEAD -> " / "<remote>/HEAD" from the decoration so only
  // branch and tag names are shown.
  std::string& bt = fields_[kBranch];
  if (!bt.empty()) {
    // Trim leading space.
    if (bt[0] == ' ')
      bt.erase(0, 1);

    size_t paren_open = bt.find('(');
    size_t paren_close = bt.find(')');
    if (paren_open != std::string::npos && paren_close != std::string::npos &&
        paren_open == 0) {
      auto remove = [&](const std::string& pattern) {
        size_t pos = bt.find(pattern, paren_open);
        if (pos == std::string::npos || pos + pattern.size() > paren_close)
          return false;
        bt.erase(pos, pattern.size());
        paren_close -= pattern.size();
        return true;
      };
      // Extract the HEAD branch name before stripping it.
      // With --decorate=full: "HEAD -> refs/heads/<branch>".
      {
        const std::string head_prefix = "HEAD -> refs/heads/";
        size_t head_pos = bt.find(head_prefix, paren_open);
        if (head_pos != std::string::npos &&
            head_pos + head_prefix.size() <= paren_close) {
          size_t name_end =
              bt.find_first_of(",)", head_pos + head_prefix.size());
          if (name_end != std::string::npos) {
            std::scoped_lock lock(head_branch_lock_);
            head_branch_ = bt.substr(head_pos + head_prefix.size(),
                                     name_end - head_pos - head_prefix.size());
          }
        }
      }
      remove("HEAD -> ");

      // Remove "refs/remotes/{remote}/HEAD" entries.
      for (;;) {
        size_t pos = bt.find("/HEAD", paren_open);
        if (pos == std::string::npos || pos + 5 > paren_close)
          break;
        size_t name_start = pos;
        while (name_start > 0 && bt[name_start - 1] != ' ' &&
               bt[name_start - 1] != '(')
          --name_start;
        std::string entry = bt.substr(name_start, pos + 5 - name_start);
        if (!remove(", " + entry))
          if (!remove(entry + ", "))
            remove(entry);
      }

      // If only empty parens remain, e.g. "()", clear the branch entirely.
      if (bt == "()")
        bt.clear();
    }
    // Split the cleaned decoration string into separate tag and branch names.
    // The string looks like "(refs/heads/main, tag: refs/tags/v1, ...)".
    // Walk comma-separated entries, trim whitespace, and classify each by
    // its ref prefix.
    if (bt.size() > 2) {
      size_t s = (bt.front() == '(') ? 1 : 0;
      size_t e = (bt.back() == ')') ? bt.size() - 1 : bt.size();
      size_t p = s;
      while (p < e) {
        // Skip leading whitespace of the entry.
        while (p < e && bt[p] == ' ')
          ++p;
        // Find the end of this entry (next comma or end of string).
        size_t comma = bt.find(',', p);
        size_t ne = (comma != std::string::npos && comma < e) ? comma : e;
        // Trim trailing whitespace.
        size_t tr = ne;
        while (tr > p && bt[tr - 1] == ' ')
          --tr;
        if (tr > p) {
          std::string name = bt.substr(p, tr - p);
          if (name.starts_with("tag: refs/tags/")) {
            info.tags.push_back(name.substr(15));
          } else if (name.starts_with("refs/remotes/")) {
            info.branches.push_back({name.substr(13), true});
          } else if (name.starts_with("refs/heads/")) {
            info.branches.push_back({name.substr(11), false});
          } else if (name == "refs/stash") {
            info.is_stash = true;
          }
        }
        p = ne + 1;
      }
    }
  }

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
