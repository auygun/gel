// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_MARKDOWN_RENDERER_H
#define GEL_UI_MODULES_MARKDOWN_RENDERER_H

#include <string>
#include <vector>

#include "base/text_search.h"
#include "gel/ui/modules/search_bar.h"
#include "third_party/imgui/imgui/imgui.h"

// Parses a subset of markdown into renderable blocks and renders them
// using ImGui.  Supports headings, lists, code blocks, paragraphs,
// inline bold/code, and text search with match highlighting.
//
// Owns everything about searching the document it holds: the search bar, the
// matches, the Ctrl+F and Escape gestures and the case-sensitivity toggle. The
// caller places the widget and is told, through search_active(), when the bar
// has a claim on Escape.
class MarkdownRenderer {
 public:
  // Parse markdown text into renderable blocks.  Call once.
  void Parse(const char* data, size_t len);
  bool parsed() const { return parsed_; }

  // Renders the search bar, when open, above the document, which goes in a
  // scrollable child window of |size|. |id| must be unique among the siblings
  // of the window this is placed in.
  // Returns true when a link was activated (caller should set animation timer).
  bool Render(const char* id, ImVec2 size);

  // Returns true while the search bar is open. A host that also acts on
  // Escape has to defer to the bar, which closes on it.
  bool search_active() const { return search_active_; }

  // Queues a jump to the next (|direction| > 0) or previous heading, applied
  // on the next Render(). Headings are ## level.
  void ScrollToHeading(int direction) { scroll_heading_dir_ = direction; }

  // Returns true when the right-click context menu is open.
  bool context_menu_open() const { return context_menu_open_; }

  // Text geometry recorded during Render() for mouse hit-testing.
  struct TextRect {
    float x, y, w, h;
    float font_scale;
    int block;
    int char_start, char_end;
  };

 private:
  struct Block {
    enum Type { kParagraph, kHeading, kList, kCodeBlock, kEmpty };
    Type type;
    std::string text;
    int level;
    std::string search_text;
  };

  struct Match {
    int block;
    int offset;
    int length;
  };

  std::vector<Block> blocks_;
  bool parsed_ = false;

  SearchBar search_bar_;
  bool search_active_ = false;
  base::MatchOptions search_options_;
  std::vector<Match> matches_;
  int current_match_ = -1;
  int scroll_to_match_ = -1;
  // What the current matches were built from.
  base::SearchTerm last_needle_;
  // Reusable fold buffer, avoids allocating per block while searching.
  std::string match_buf_;

  // Heading positions recorded during Render() (content-space Y).
  struct HeadingPos {
    int block_index;
    float y;
  };
  std::vector<HeadingPos> heading_positions_;
  int scroll_to_block_ = -1;
  int scroll_heading_dir_ = 0;

  std::vector<TextRect> text_rects_;

  // Character-level text selection state.
  int sel_start_block_ = -1;
  int sel_start_char_ = 0;
  int sel_end_block_ = -1;
  int sel_end_char_ = 0;
  bool sel_dragging_ = false;
  bool context_menu_open_ = false;

  void UpdateSearchBar();
  void FindMatches(const base::SearchTerm& needle);
  void SetCurrentMatch(int index);

  // Renders the document blocks. Expects to be inside the child window.
  bool RenderBlocks();

  // Keyboard scrolling and queued heading jumps, inside the child window.
  void UpdateScroll();
  void ScrollToNextHeading(float current_scroll_y);
  void ScrollToPrevHeading(float current_scroll_y);

  void MouseToPos(float mx, float my, int& block, int& char_offset) const;
  std::string GetSelectedText() const;
};

#endif  // GEL_UI_MODULES_MARKDOWN_RENDERER_H
