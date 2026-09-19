// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_SEARCH_BAR_H
#define GEL_UI_MODULES_SEARCH_BAR_H

#include <functional>
#include <string>

#include "base/text_search.h"

// A reusable search input row: text field, case-sensitivity toggle, stacked
// previous/next buttons, match counter and close button. Owns the input buffer
// and the keyboard focus handling; the caller owns the actual searching and
// feeds the resulting match state back in through Options::matches.
//
// The bar only reports intent. Navigation, closing and re-running the search
// are all up to the caller, which keeps the widget independent of the search
// backend behind it.
class SearchBar {
 public:
  // State of the caller's search backend. Drives the match counter and the
  // enabled state of the navigation buttons.
  struct MatchState {
    int current = -1;      // 0-based index of the current match, -1 if none.
    int total = 0;         // Matches found so far.
    bool complete = true;  // False while the backend is still searching.
  };

  // Picks when the navigation buttons are enabled.
  enum class NavEnable {
    kHasMatches,  // Once the backend reports at least one match.
    kHasTerm,     // As soon as the field is non-empty.
  };

  struct Options {
    // Prefix for the ImGui widget ids. Must be unique per search bar.
    const char* id = "search";
    // Input field width. 0 picks a default wide enough for a commit hash.
    float width = 0.0f;
    bool case_toggle = true;
    bool whole_word_toggle = true;
    bool nav_buttons = true;
    bool match_counter = false;
    bool close_button = false;
    NavEnable nav_enable = NavEnable::kHasMatches;
    MatchState matches;
    // Progress of an in-flight search in [0,1], drawn as a fill behind the
    // input field. Negative hides it.
    float progress = -1.0f;
    // Also navigate on Shift+Up / Shift+Down, for a bar that is always
    // visible and owns those keys.
    bool arrow_shortcuts = false;
    // Tooltip for the input field. Suppressed while the field has focus.
    const char* input_tooltip = nullptr;
    // Shows a tooltip for the last item. Defaults to a delayed SetTooltip().
    // Pass a callback to join a shared tooltip group (see ItemTooltip).
    std::function<void(const char*)> tooltip;
    // Called right after the input field with its buffer, for custom input
    // handling (e.g. a right-click context menu).
    std::function<void(char*, size_t)> on_input;
  };

  // What the user asked for this frame.
  struct Result {
    bool term_changed = false;
    bool next = false;  // Enter, next button or Shift+Down.
    bool prev = false;  // Shift+Enter, previous button or Shift+Up.
    bool close_requested = false;
    bool input_active = false;
    bool input_deactivated = false;
  };

  // Renders the bar. |match| is read to draw the toggles and written when the
  // user clicks one, so callers can bind a persisted setting directly.
  // The match counter reflects |opts.matches|, so a caller that re-runs its
  // search from the returned intents shows the new count one frame later.
  Result Update(const Options& opts, base::MatchOptions* match);

  // Focuses the input field on the next Update().
  void Focus() { focus_ = true; }

  // Focuses the input field, dropping |c| from the input for a few frames.
  // Lets a single-character shortcut focus the field without the shortcut key
  // itself leaking in through ImGui's event queue.
  void FocusRejectingChar(char c);

  const char* term() const { return input_; }
  bool empty() const { return input_[0] == '\0'; }
  void Clear() { input_[0] = '\0'; }

 private:
  static constexpr int kInputSize = 256;
  char input_[kInputSize] = {};

  std::string prev_term_;
  std::string counter_;
  bool focus_ = false;
  char reject_char_ = 0;
  int reject_frames_ = 0;
};

#endif  // GEL_UI_MODULES_SEARCH_BAR_H
