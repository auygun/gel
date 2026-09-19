// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/blame_lookup.h"

#include <algorithm>
#include <cstdlib>

#include "gel/commands/git_diff.h"
#include "gel/commands/git_log.h"
#include "gel/git_repo.h"
#include "gel/ui/lower_panel/commit_diff.h"
#include "gel/ui/upper_panel/commit_history.h"

BlameLookup::BlameLookup(CommitHistory& commit_history,
                         CommitDiff& commit_diff,
                         GitDiff& git_diff,
                         GitLog& git_log,
                         GitRepo& git_repo,
                         std::function<void(bool)> busy_callback,
                         PopupCallback show_popup)
    : commit_history_(commit_history),
      commit_diff_(commit_diff),
      git_diff_(git_diff),
      git_log_(git_log),
      git_repo_(git_repo),
      busy_callback_(std::move(busy_callback)),
      show_popup_(std::move(show_popup)),
      git_blame_(busy_callback_) {}

std::string BlameLookup::GetRevision(const std::string& file,
                                     int line_number,
                                     bool is_old_side) const {
  std::string commit = commit_history_.GetSelectedCommit();
  if (commit_history_.IsUnstagedSelected() ||
      commit_history_.IsStagedSelected()) {
    return "HEAD";
  }
  return is_old_side ? (commit + "^") : commit;
}

void BlameLookup::ShowLineOrigin(const std::string& file,
                                 int line_number,
                                 bool is_old_side) {
  std::string revision = GetRevision(file, line_number, is_old_side);
  busy_ = true;
  git_blame_.Run(revision, file, line_number, git_repo_.toplevel());
}

void BlameLookup::PollBlame() {
  GitBlame::Result result;
  if (!git_blame_.Update(result))
    return;

  busy_ = false;

  if (!result.success) {
    show_popup_("Error", "Could not determine the origin of this line.");
    return;
  }

  if (result.hash.find_first_not_of('0') == std::string::npos) {
    show_popup_("Line origin", "This line has not been committed yet.");
    return;
  }

  auto commits = git_log_.GetCommits();
  auto it = std::find_if(commits.begin(), commits.end(), [&](const auto& c) {
    return c.commit == result.hash;
  });
  if (it == commits.end()) {
    show_popup_("Commit not loaded", "Commit " + result.hash.substr(0, 12) +
                                         " is not in the loaded commit list.");
    return;
  }

  int row =
      static_cast<int>(it - commits.begin()) + commit_history_.SyntheticRows();
  bool same_commit = row == commit_history_.GetSelectedRowIndex();
  if (!same_commit) {
    commit_history_.SelectCommit(row);
    commit_history_.ScrollToSelected();
  }
  pending_file_ = result.file;
  pending_commit_ = result.hash;
  pending_line_ = result.line;
  scan_file_idx_ = -1;
  scan_pos_ = -1;
  scan_file_count_ = -1;
  scan_new_line_ = 0;
  scan_in_hunk_ = false;
  wait_for_clear_ = !same_commit;
}

void BlameLookup::Cancel() {
  pending_file_.clear();
  pending_commit_.clear();
  pending_line_ = 0;
  scan_file_idx_ = -1;
  scan_pos_ = -1;
  scan_file_count_ = -1;
  git_blame_.Kill();
  busy_ = false;
}

void BlameLookup::OnCommitChanged(const std::string& commit, bool reselected) {
  if (reselected)
    return;
  // Scrolling the table to the commit this lookup navigated to re-selects the
  // same row, which reports as a selection change. Keep the scan alive: only a
  // move to a different commit invalidates it.
  if (!pending_commit_.empty() && commit == pending_commit_)
    return;
  Cancel();
}

void BlameLookup::OnDiffCleared() {
  if (!pending_file_.empty()) {
    scan_file_idx_ = -1;
    scan_pos_ = -1;
    scan_file_count_ = -1;
    wait_for_clear_ = false;
  }
}

void BlameLookup::ScanDiffContent() {
  if (pending_file_.empty() || wait_for_clear_)
    return;

  auto files = git_diff_.GetFileList();
  auto content = git_diff_.GetDiffContent();
  int prev_scan_pos = scan_pos_;
  int prev_file_count = scan_file_count_;
  scan_file_count_ = static_cast<int>(files.size());

  // Find the file in the file list (once).
  if (scan_file_idx_ < 0) {
    for (size_t i = 0; i < files.size(); i++) {
      if (files[i].status != FileStatus::kCommit &&
          files[i].path == pending_file_) {
        scan_file_idx_ = static_cast<int>(i);
        scan_pos_ = static_cast<int>(files[i].start_line);
        scan_new_line_ = 0;
        scan_in_hunk_ = false;
        break;
      }
    }
  }

  if (scan_file_idx_ >= 0) {
    int end_line = static_cast<int>(content.size());
    if (scan_file_idx_ + 1 < static_cast<int>(files.size()))
      end_line = std::min(
          end_line, static_cast<int>(files[scan_file_idx_ + 1].start_line));

    int new_line = scan_new_line_;
    bool in_hunk = scan_in_hunk_;
    bool found = false;

    for (int j = scan_pos_; j < end_line; j++) {
      const std::string& text = content[j];

      if (text.starts_with("@@")) {
        size_t plus = text.find('+', 3);
        if (plus != std::string::npos)
          new_line = std::atoi(text.c_str() + plus + 1);
        in_hunk = true;
        continue;
      }

      if (!in_hunk)
        continue;

      char ch = text.empty() ? '\0' : text[0];

      if ((ch == ' ' || ch == '+') && new_line == pending_line_) {
        commit_diff_.HighlightLine(j);
        pending_file_.clear();
        pending_line_ = 0;
        scan_file_idx_ = -1;
        found = true;
        break;
      }

      if (ch == ' ' || ch == '+')
        new_line++;
      // '-' lines don't increment new_line.
    }

    if (!found) {
      // Save progress for the next frame. Use max to avoid going
      // backwards when end_line is clamped to content.size().
      scan_pos_ = std::max(scan_pos_, end_line);
      scan_new_line_ = new_line;
      scan_in_hunk_ = in_hunk;

      // Give up only when the worker is done AND no progress was made
      // since the last frame. This avoids a race where busy() returns
      // false before the final Merge() has delivered all content.
      if (!git_diff_.busy() && scan_pos_ == prev_scan_pos) {
        pending_file_.clear();
        pending_line_ = 0;
        scan_file_idx_ = -1;
      }
    }
  } else if (!git_diff_.busy() && scan_file_count_ == prev_file_count) {
    // Same race as above: the worker can go idle with its last file entries
    // still waiting for a merge, so only give up once the list stops growing.
    pending_file_.clear();
    pending_line_ = 0;
  }
}
