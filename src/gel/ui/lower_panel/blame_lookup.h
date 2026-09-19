// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_BLAME_LOOKUP_H
#define GEL_UI_LOWER_PANEL_BLAME_LOOKUP_H

#include <functional>
#include <string>

#include "gel/commands/git_blame.h"

class CommitDiff;
class CommitHistory;
class GitDiff;
class GitLog;
class GitRepo;

// Encapsulates blame lookup logic: running "git blame" to find line origins
// and scanning diff content for the target line.
class BlameLookup {
 public:
  using PopupCallback =
      std::function<void(const std::string& title, std::string message)>;

  BlameLookup(CommitHistory& commit_history,
              CommitDiff& commit_diff,
              GitDiff& git_diff,
              GitLog& git_log,
              GitRepo& git_repo,
              std::function<void(bool)> busy_callback,
              PopupCallback show_popup);

  // Looks up the origin of a diff line by running "git blame --porcelain",
  // then navigates to the originating commit and highlights the line.
  void ShowLineOrigin(const std::string& file,
                      int line_number,
                      bool is_old_side);

  // Cancels any in-progress blame search.
  void Cancel();

  // Called when the selected commit changes. Cancels any pending blame scan,
  // unless this is a re-selection of the row the scan itself navigated to.
  void OnCommitChanged(const std::string& commit, bool reselected);

  // Called when the diff content is cleared (new diff started).
  void OnDiffCleared();

  // Incrementally scans diff content for the pending blame target line.
  // Should be called each frame from the main update loop.
  void ScanDiffContent();

  // Polls the blame worker for completion and processes the result.
  void PollBlame();

  bool busy() const { return busy_; }

 private:
  CommitHistory& commit_history_;
  CommitDiff& commit_diff_;
  GitDiff& git_diff_;
  GitLog& git_log_;
  GitRepo& git_repo_;
  std::function<void(bool)> busy_callback_;
  PopupCallback show_popup_;

  GitBlame git_blame_;
  bool busy_ = false;
  std::string pending_file_;
  std::string pending_commit_;
  int pending_line_ = 0;
  int scan_file_idx_ = -1;
  int scan_pos_ = -1;
  int scan_file_count_ = -1;
  int scan_new_line_ = 0;
  bool scan_in_hunk_ = false;
  bool wait_for_clear_ = false;

  std::string GetRevision(const std::string& file,
                          int line_number,
                          bool is_old_side) const;
};

#endif  // GEL_UI_LOWER_PANEL_BLAME_LOOKUP_H
