// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_COMMIT_DIFF_H
#define GEL_UI_LOWER_PANEL_COMMIT_DIFF_H

#include <functional>
#include <string>

#include "gel/ui/lower_panel/diff_content.h"
#include "gel/ui/lower_panel/file_list.h"

class GitCmdRunner;
class GitDiff;
class GitLog;
class PersistentSettings;

// Lower panel: file list and colored diff output for the selected commit.
class CommitDiff : public FileList::Delegate, public DiffContent::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // Returns the index of the selected row in GitLog::GetCommits(),
    // or -1 if no real commit is selected (e.g. unstaged/staged changes).
    virtual int GetSelectedCommitIndex() const = 0;
    virtual void OnShowImageDiff(const std::string& old_ref,
                                 const std::string& new_ref,
                                 const std::string& path,
                                 bool new_from_worktree = false) = 0;
    virtual void SetPrimarySelection(const std::string& text) = 0;
    virtual void OnShowLineOrigin(const std::string& file,
                                  int line_number,
                                  bool is_old_side) = 0;
    virtual void OnRunGitGuiBlame(const std::string& file,
                                  int line_number,
                                  bool is_old_side) = 0;
    virtual bool IsUnstagedSelected() const = 0;
    virtual bool IsStagedSelected() const = 0;
    virtual void OnBlameSearchCancel() = 0;
    virtual void UpdateLinesOfContext(int ctx) = 0;
    virtual void OnClearDiffPathFilterRequested() = 0;
    virtual void OnDiffRefreshRequested() = 0;
    virtual bool HasDiffPathFilter() const = 0;
    virtual void OnNavigateToCommit(const std::string& commit) = 0;
  };

  CommitDiff(Delegate& delegate,
             GitDiff& git_diff,
             GitLog& git_log,
             GitCmdRunner& runner,
             PersistentSettings& settings,
             FileList::ConfirmCallback on_confirm,
             std::function<void()> main_thread_busy);

  // Renders the file list and diff content panels.
  void Update(const std::string& search_term);

  // Resets the panel state (called when new diff data arrives).
  void Reset();
  void RevertAll();

  // Scrolls the diff content to the top.
  void ScrollToTop();

  // Captures the diff scroll position so it can be restored after the diff is
  // re-run with different options (e.g. lines of context).
  void CaptureScrollAnchor();

  // Remembers the diff scroll position of the commit being left, and restores
  // the one remembered for a commit the user navigates back to.
  void SaveScrollPosition();
  void RestoreSavedScrollPosition(const std::string& commit,
                                  bool unstaged,
                                  bool staged);

  void CancelSearch();

  // Scrolls to and highlights the given diff line.
  void HighlightLine(int line);

 private:
  // FileList::Delegate implementation
  int GetSelectedCommitIndex() const final;
  bool IsUnstagedSelected() const final;
  bool IsStagedSelected() const final;
  void OnShowImageDiff(const std::string& old_ref,
                       const std::string& new_ref,
                       const std::string& path,
                       bool new_from_worktree) final;

  // DiffContent::Delegate implementation
  void SetPrimarySelection(const std::string& text) final;
  void OnShowLineOrigin(const std::string& file,
                        int line_number,
                        bool is_old_side) final;
  void OnRunGitGuiBlame(const std::string& file,
                        int line_number,
                        bool is_old_side) final;
  void OnBlameSearchCancel() final;
  void OnNavigateToCommit(const std::string& commit) final;

  void RenderOverlayControls();

  Delegate& delegate_;
  PersistentSettings& settings_;
  GitDiff& git_diff_;

  FileList file_list_;
  DiffContent diff_content_;

  bool overlay_tooltip_suppressed_ = false;
};

#endif  // GEL_UI_LOWER_PANEL_COMMIT_DIFF_H
