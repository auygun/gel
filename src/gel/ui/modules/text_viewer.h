// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_TEXT_VIEWER_H
#define GEL_UI_MODULES_TEXT_VIEWER_H

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "gel/ui/modules/search_bar.h"
#include "gel/ui/modules/text_viewer_search.h"
#include "third_party/imgui/imgui/imgui.h"

struct ImDrawList;

// A reusable ImGui widget for displaying colored text content with:
// - Efficient rendering via ImGuiListClipper
// - Character-level text selection (click, drag, double-click word select)
// - Double-precision scrolling for very large content
// - Built-in search bar (Ctrl+F) with time-sliced incremental search
// - Selection and search match highlighting
// - Keyboard shortcuts (Ctrl+A, Ctrl+C, Ctrl+F)
// - Context menu (Select all, Copy)
class TextViewer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when the user completes a text selection via mouse drag.
    virtual void OnTextSelected(const std::string& text) {}

    // Called on right-click before the context menu opens.
    virtual void OnRightClick(int line_index, int char_offset) {}

    // Called on Ctrl+Left-click. Return true to consume the click (prevents
    // text selection from starting).
    virtual bool OnCtrlClick(int line_index, int char_offset) { return false; }

    // Called for each visible line to draw custom backgrounds before the text.
    virtual void DrawLineBackground(int line_index,
                                    const std::string& line,
                                    ImDrawList* draw_list,
                                    ImVec2 line_pos,
                                    float line_height,
                                    float window_x,
                                    float window_width) {}

    // Called to render a line's text. The default implementation renders the
    // string as plain text with the default text color.
    virtual void RenderLine(int line_index, const std::string& line);

    // Returns the text offset in pixels for a given line index, used to
    // align selection highlights with the actual rendered text position.
    // Override this when RenderLine advances the cursor before rendering
    // (e.g. drawing a badge before the text).
    virtual float GetTextOffset(int line_index) const;

    // Called for each visible line to draw custom highlights over the text
    // (e.g. external search term highlighting).
    virtual void DrawLineHighlights(int line_index,
                                    const std::string& plain_text,
                                    ImDrawList* draw_list,
                                    ImVec2 line_pos,
                                    float line_height,
                                    ImFont* font,
                                    float font_size) {}

    // Called to add custom items to the right-click context menu.
    virtual void OnContextMenu(int line_index) {}

    // Called after the search bar InputText for custom input handling
    // (e.g. right-click context menu on the search field).
    virtual void OnSearchInput(char* buf, size_t buf_size) {}
  };

  TextViewer(Delegate& delegate,
             std::function<void()> search_in_progress_callback = nullptr);

  ~TextViewer();

  // Renders the text viewer widget. |width| is the child window width.
  // |content| is the lines to display. |match_options| is the search bar's
  // match options, read and written in place so the caller can bind a
  // persisted setting.
  void Update(float width,
              std::span<const std::string> content,
              base::MatchOptions* match_options);

  // Scrolls to the given line index.
  void ScrollToLine(int line);

  // Scrolls to the top.
  void ScrollToTop();

  // Returns the line index visible at the top of the viewport, or -1 if the
  // scroll position was set programmatically in the last few frames.
  int GetTopVisibleLine() const;

  // Scrolls to and highlights the given line with selection background.
  void HighlightLine(int line);

  // Returns true if the user moved the view on the last Update(): mouse wheel,
  // keyboard scrolling, a scrollbar drag, or a jump to a search match. Lets a
  // caller driving the scroll position hand control back once the user takes
  // over. Scrolls the caller itself requested do not count.
  bool user_moved_view() const { return user_moved_view_; }

  void Reset();
  void CancelSearch();

  // Returns true if the search bar is currently visible.
  bool search_active() const { return search_active_; }

  // Enables keyboard scrolling (arrow keys, page up/down, home/end).
  // Disabled by default.
  void set_keyboard_scroll(bool enabled) { keyboard_scroll_ = enabled; }

  // Returns the top-right corner of the content child window, inset by the
  // scrollbar. Valid after Update().
  ImVec2 content_top_right() const { return content_top_right_; }

 private:
  Delegate& delegate_;

  int scroll_to_line_ = -1;
  double scroll_exact_target_ = -1;
  float prev_scroll_y_ = 0;
  int suppress_scroll_report_ = 0;
  int line_count_ = 0;
  bool track_top_line_ = true;
  int top_visible_line_ = -1;
  bool user_moved_view_ = false;

  // Character-level text selection state.
  int sel_start_line_ = -1;
  int sel_start_char_ = 0;
  int sel_end_line_ = -1;
  int sel_end_char_ = 0;
  bool sel_dragging_ = false;
  bool sel_word_mode_ = false;
  int sel_anchor_start_ = 0;
  int sel_anchor_end_ = 0;
  int sel_anchor_line_ = -1;
  bool keyboard_scroll_ = false;
  bool wheel_intercepted_ = false;
  int wheel_scroll_frames_ = 0;
  int ctx_menu_line_ = -1;
  int highlight_line_ = -1;
  bool scroll_to_highlight_ = false;
  float content_width_ = 0;

  // Search state.
  TextViewerSearch search_;
  SearchBar search_bar_;
  bool search_active_ = false;
  // Reusable fold buffer for painting highlights on the visible lines.
  std::string match_buf_;
  ImVec2 content_top_right_{0, 0};
  std::function<void()> search_in_progress_callback_;

  void UpdateSearchBar(std::span<const std::string> content,
                       base::MatchOptions* match_options,
                       bool has_focus);
  void HandleKeyboardShortcuts(std::span<const std::string> content,
                               int s_line,
                               int s_char,
                               int e_line,
                               int e_char);
  void UpdateContextMenu(std::span<const std::string> content,
                         int s_line,
                         int s_char,
                         int e_line,
                         int e_char);

  // Helper: perform Select All.
  void SelectAll(std::span<const std::string> content);

  // Helper: extract selected text from content using given selection bounds.
  // The selection bounds are stripped-relative (\x01 prefix skipped).
  std::string ExtractSelectedText(std::span<const std::string> content,
                                  int s_line,
                                  int s_char,
                                  int e_line,
                                  int e_char) const;
};

#endif  // GEL_UI_MODULES_TEXT_VIEWER_H
