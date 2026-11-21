// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/blame_lookup.h"

#include <algorithm>
#include <optional>

#include "base/exec.h"

#include "gel/commands/git_diff.h"
#include "gel/commands/git_log.h"
#include "gel/git_repo.h"
#include "gel/ui/lower_panel/commit_diff.h"
#include "gel/ui/upper_panel/commit_history.h"
#include "third_party/kaliber/base/thread_pool.h"

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

std::string BlameLookup::GetRevision(int parent) const {
  std::string commit = commit_history_.GetSelectedCommit();
  if (commit_history_.IsUnstagedSelected() ||
      commit_history_.IsStagedSelected()) {
    return "HEAD";
  }
  return parent > 0 ? commit + "^" + std::to_string(parent) : commit;
}

void BlameLookup::ShowLineOrigin(const std::string& file,
                                 int line_number,
                                 int parent) {
  ShowLineOriginAt(GetRevision(parent), file, line_number);
}

void BlameLookup::ShowLineOriginAt(const std::string& revision,
                                   const std::string& file,
                                   int line_number) {
  ++lookup_id_;
  busy_ = true;
  git_blame_.Run(revision, file, line_number, git_repo_.toplevel());
}

void BlameLookup::ShowStagedLineOrigin(const std::string& blob,
                                       const std::string& file,
                                       int line_number) {
  int id = ++lookup_id_;
  busy_ = true;
  // Read as is, so its lines are what git compares with HEAD's. Off the main
  // thread: the file can be large.
  std::filesystem::path toplevel = git_repo_.toplevel();
  std::weak_ptr<bool> alive = alive_;
  base::ThreadPool::Get()
      .PostTaskAndReplyWithResult<std::optional<std::string>>(
          HERE,
          [blob, toplevel]() -> std::optional<std::string> {
            std::vector<std::string> args = {"git"};
            if (!toplevel.empty())
              args.insert(args.end(), {"-C", toplevel.string()});
            args.insert(args.end(), {"cat-file", "blob", blob});
            Exec exec;
            if (!exec.Start(args))
              return std::nullopt;
            while (exec.Poll()) {
            }
            if (exec.GetStatus() != Exec::Status::EXITED ||
                exec.GetResult() != 0)
              return std::nullopt;
            return std::move(exec.GetOut());
          },
          [this, alive, id, file,
           line_number](std::optional<std::string> data) {
            // Dropped if this object is gone, or the lookup was cancelled or
            // superseded meanwhile.
            if (alive.expired() || id != lookup_id_)
              return;
            if (!data) {
              busy_ = false;
              show_popup_("Error", "Could not read the staged file.");
              return;
            }
            git_blame_.Run("HEAD", file, line_number, git_repo_.toplevel(),
                           std::move(data));
          });
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
  if (show_line_callback_ && show_line_callback_(result.file, result.line))
    return;
  pending_file_ = result.file;
  pending_commit_ = result.hash;
  pending_line_ = result.line;
  scan_file_idx_ = -1;
  scan_pos_ = -1;
  scan_file_count_ = -1;
  wait_for_clear_ = !same_commit;
}

void BlameLookup::Cancel() {
  ++lookup_id_;
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
  auto line_numbers = git_diff_.GetLineNumbers();
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
        break;
      }
    }
  }

  if (scan_file_idx_ >= 0) {
    int end_line = static_cast<int>(line_numbers.size());
    if (scan_file_idx_ + 1 < static_cast<int>(files.size()))
      end_line = std::min(
          end_line, static_cast<int>(files[scan_file_idx_ + 1].start_line));

    // Blame reports the line number in the commit's version of the file,
    // which is the result side of its diff, for normal and combined diffs.
    bool found = false;
    for (int j = scan_pos_; j < end_line; j++) {
      if (line_numbers[j].new_line == pending_line_) {
        commit_diff_.HighlightLine(j);
        pending_file_.clear();
        pending_line_ = 0;
        scan_file_idx_ = -1;
        found = true;
        break;
      }
    }

    if (!found) {
      // Save progress for the next frame. Use max to avoid going
      // backwards when end_line is clamped to the parsed content.
      scan_pos_ = std::max(scan_pos_, end_line);

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
