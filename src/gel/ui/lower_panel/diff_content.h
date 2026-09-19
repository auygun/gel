// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_LOWER_PANEL_DIFF_CONTENT_H
#define GEL_UI_LOWER_PANEL_DIFF_CONTENT_H

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/text_search.h"
#include "gel/ui/modules/text_viewer.h"
#include "gel/ui/syntax_highlight.h"

class GitDiff;
class PersistentSettings;

// Renders the diff content panel with text selection and search.
// Wraps TextViewer and adds Gel-specific behavior: file header backgrounds,
// +/- line tinting, syntax highlighting, commit message search highlighting,
// blame context menu items, and auto-file-selection tracking.
class DiffContent : public TextViewer::Delegate {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
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
    virtual void OnNavigateToCommit(const std::string& commit) = 0;
  };

  DiffContent(Delegate& delegate,
              GitDiff& git_diff,
              PersistentSettings& settings,
              std::function<void()> main_thread_busy);

  ~DiffContent();

  // Renders the diff content panel. |width| is the child window width.
  // |search_term| highlights commit message matches from the toolbar search.
  void Update(float width, const std::string& search_term);

  // Scrolls to the given diff line. Cancels a pending scroll anchor: this is
  // the user navigating, which outranks restoring an earlier position.
  void ScrollToLine(int line);

  // Scrolls to the top of the diff.
  void ScrollToTop();

  // Returns the file index whose diff section is visible at the top,
  // or -1 if none. Resets after being read.
  int ConsumeAutoSelectedFile();

  // Scrolls to and highlights the given diff line with selection background.
  void HighlightLine(int line);

  // Captures the current scroll position as a content anchor (file plus source
  // line number). The position is restored once the diff has been re-run and
  // the content covering it has been parsed. Call right before re-running the
  // diff with different options (e.g. lines of context) so the user keeps
  // looking at the same code. Does nothing if an anchor is still pending.
  void CaptureScrollAnchor();

  // Remembers where the diff currently shown is scrolled to, keyed by the
  // commit it belongs to. Call when leaving a commit.
  void SaveScrollPosition();

  // Restores the position remembered for the given commit (empty hash for the
  // unstaged/staged rows, told apart by the flags) once its diff has been
  // re-run. Does nothing if nothing was remembered for it.
  void RestoreSavedScrollPosition(const std::string& commit,
                                  bool unstaged,
                                  bool staged);

  void Reset();
  void CancelSearch();

  // Returns the top-right anchor for overlay controls, inset from the
  // scrollbar and below the search bar. Valid after Update().
  ImVec2 GetOverlayAnchor() const { return overlay_anchor_; }

 private:
  // TextViewer::Delegate implementation.
  void OnTextSelected(const std::string& text) override;
  void OnRightClick(int line_index, int char_offset) override;
  bool OnCtrlClick(int line_index, int char_offset) override;
  void DrawLineBackground(int line_index,
                          const std::string& line,
                          ImDrawList* draw_list,
                          ImVec2 line_pos,
                          float line_height,
                          float window_x,
                          float window_width) override;
  void RenderLine(int line_index, const std::string& line) override;
  float GetTextOffset(int line_index) const override;
  void DrawLineHighlights(int line_index,
                          const std::string& plain_text,
                          ImDrawList* draw_list,
                          ImVec2 line_pos,
                          float line_height,
                          ImFont* font,
                          float font_size) override;
  void OnContextMenu(int line_index) override;
  void OnSearchInput(char* buf, size_t buf_size) override;

  void ComputeLineOrigin(int file_idx,
                         std::string& out_file,
                         int& out_line,
                         bool& out_is_old,
                         int target_line);

  bool ShouldShowLineNumbers(int line_index) const;

  // Scrolls to |line| without touching a pending scroll anchor.
  void ScrollToLineInternal(int line);

  // Returns {old_digits, new_digits} for line number columns.
  std::pair<int, int> ComputeLineNumberDigits() const;

  // Finds the first '+' or '-' sign character at the start of |line|,
  // respecting combined-diff @-count (skipping leading '@' prefix chars).
  // Returns the sign character, or std::nullopt if none found.
  std::optional<char> FindDiffSign(int line_index, const std::string& line);

  Delegate& delegate_;
  GitDiff& git_diff_;
  PersistentSettings& settings_;

  TextViewer text_viewer_;

  int auto_selected_file_ = -1;
  int ctx_menu_char_ = 0;

  // Last line reported at the top of the viewport, kept across reloads so a
  // scroll anchor can still be captured while the viewer reports no position.
  int last_top_line_ = 0;

  // Diff worker state at the end of the last frame, used to tell a finished
  // re-run from one that is still streaming lines in.
  bool diff_was_busy_ = false;
  size_t last_content_size_ = 0;

  // Scroll position captured before a diff re-run, restored once the new
  // content covering it has been parsed.
  struct ScrollAnchor {
    // Diff identity at capture time. The anchor is dropped if the panel moved
    // on to a different commit in the meantime.
    std::string commit;
    bool unstaged = false;
    bool staged = false;
    // Anchored file, by index with the path as the identity check.
    int file_index = -1;
    std::string file;
    // Source line numbers of the anchored line, 0 if it has none, and which
    // of the two sides identifies it.
    int old_line = 0;
    int new_line = 0;
    bool use_old = false;
    // Distance from the top line to the numbered line the anchor was taken
    // from, negative when that line is above the top line.
    int lead = 0;
    // Fallback for lines with no numbers: offset from the file's start line.
    int line_offset = 0;
    // Set once the old content has been cleared, so the anchor is not matched
    // against the content it was captured from.
    bool armed = false;
    // Resume position of the scan for the anchored line, and the line it
    // settled on. Both index into the content being parsed, so a further
    // re-run invalidates them.
    int scan_pos = -1;
    int resolved_line = -1;
    // Top line the viewport is expected to report while the anchor is pending:
    // the captured line until the content is cleared, then the top of the
    // reloading content. Anything else means the user scrolled, which drops
    // the anchor.
    int expected_top = 0;
  };
  std::optional<ScrollAnchor> scroll_anchor_;

  // Positions remembered per commit for navigating the selection history,
  // oldest first. Bounded: the history itself is not.
  static constexpr size_t kMaxSavedPositions = 64;
  std::vector<ScrollAnchor> saved_positions_;

  // Builds an anchor for what the viewport is showing, or nothing when it is
  // at the top or has no content.
  std::optional<ScrollAnchor> MakeScrollAnchor() const;

  // Restores the pending scroll anchor. Returns true once the anchor has been
  // consumed (position restored, or dropped because it can no longer be
  // matched), false while waiting for more diff output.
  bool TryRestoreScrollAnchor(ScrollAnchor& anchor);

  // Max line numbers for dynamic column width.
  int max_old_line_ = 0;
  int max_new_line_ = 0;

  // State for commit message search highlighting. The term is rebuilt only
  // when it or the case setting changes, not per highlighted line.
  std::string search_term_;
  base::SearchTerm search_needle_;
  base::MatchOptions search_needle_options_;
  std::string match_buf_;
  int commit_msg_end_ = 0;

  // Cached language detection for syntax highlighting.
  Language current_lang_ = Language::kNone;
  int current_lang_file_idx_ = -1;

  ImVec2 overlay_anchor_{0, 0};
};

#endif  // GEL_UI_LOWER_PANEL_DIFF_CONTENT_H
