// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UPPER_PANEL_COMMIT_HISTORY_H
#define GEL_UI_UPPER_PANEL_COMMIT_HISTORY_H

#include <functional>
#include <string>
#include <vector>

#include "gel/ui/upper_panel/commit_context_menu.h"
#include "gel/ui/upper_panel/commit_graph.h"
#include "gel/ui/upper_panel/commit_modal.h"

class GitCmdRunner;
class GitLog;
class GitRepo;
class PersistentSettings;

// Upper panel: scrollable table of commits (message, author, date).
class CommitHistory {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    // |reselected| is set when the already-selected row was clicked again.
    // |from_history| is set when the selection came from navigating the
    // selection history with Left/Right.
    virtual void OnCommitSelected(const std::string& commit,
                                  bool reselected,
                                  bool from_history) = 0;
    virtual void OnRevertAllRequested() = 0;
  };

  CommitHistory(Delegate& delegate,
                GitLog& git_log,
                GitCmdRunner& runner,
                GitRepo& git_repo,
                PersistentSettings& settings,
                std::function<void(const std::string&)> open_in_new_window);

  // Renders the commit history table.
  void Update(const std::string& search_term);

  // Resets the scroll position to the top on the next frame.
  void ResetScrollPos();

  // Select a commit by row index.
  void SelectCommit(int row, bool skip_record = false, bool user_click = false);

  // Scroll the table to the currently selected commit and reset the search
  // start index. Called after a log refresh or commit search.
  void ScrollToSelected();

  // Resets the commit graph so it is rebuilt from scratch on the next frame.
  void ResetGraph();

  // Adjust selection state when synthetic rows appear or disappear.
  void HandleLocalStatusUpdate(bool has_unstaged, bool has_staged);

  // Auto-select the first real commit once it arrives.
  void AutoSelectFirst();

  void ClearSelectionHistory();

  void SetInfoMessage(std::string message) {
    info_message_ = std::move(message);
  }

  // Returns the selected row index, or -1 if nothing is selected.
  int GetSelectedRowIndex() const { return selected_row_; }

  bool IsUnstagedSelected() const { return unstaged_selected_; }
  bool IsStagedSelected() const { return staged_selected_; }
  const std::string& GetSelectedCommit() const { return selected_commit_; }

  // Returns the index into the commit list, or -1 if nothing or a synthetic
  // row is selected.
  int GetSelectedCommitIndex() const;

  int SyntheticRows() const {
    return (has_unstaged_changes_ ? 1 : 0) + (has_staged_changes_ ? 1 : 0);
  }

  bool ConsumePendingTagMove(std::string& name, std::string& commit) {
    return context_menu_.ConsumePendingTagMove(name, commit);
  }

 private:
  Delegate& delegate_;
  GitLog& git_log_;
  GitCmdRunner& runner_;
  CommitContextMenu context_menu_;
  PersistentSettings& settings_;
  CommitModal commit_modal_;

  int selected_row_ = -1;
  bool unstaged_selected_ = false;
  bool staged_selected_ = false;
  std::string selected_commit_;
  std::string scroll_to_commit_;
  size_t scroll_search_offset_ = 0;
  int scroll_to_row_ = -1;
  int commit_count_ = 0;

  bool has_unstaged_changes_ = false;
  bool has_staged_changes_ = false;

  bool reset_scroll_pos_ = false;
  CommitGraph graph_;

  bool tag_tooltip_suppressed_ = false;
  std::string info_message_;

  // A selection history entry. Stored row-independently so entries stay valid
  // when the synthetic (unstaged/staged) rows appear or disappear and shift
  // every row index below them.
  struct HistoryEntry {
    enum class Kind { kCommit, kUnstaged, kStaged };
    Kind kind = Kind::kCommit;
    // Index into the commit list; kCommit only. -1 for "nothing selected",
    // which never resolves back to a row.
    int commit_index = -1;

    bool operator==(const HistoryEntry&) const = default;
  };

  std::vector<HistoryEntry> selection_history_;
  int history_index_ = -1;
  bool restoring_selection_ = false;

  HistoryEntry CurrentHistoryEntry() const;
  void RecordSelection(bool skip = false);
  bool RestoreSelection(int history_index);
};

#endif  // GEL_UI_UPPER_PANEL_COMMIT_HISTORY_H
