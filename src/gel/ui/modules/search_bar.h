// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_MODULES_SEARCH_BAR_H
#define GEL_UI_MODULES_SEARCH_BAR_H

#include <functional>
#include <string>
#include <vector>

#include "base/text_search.h"

struct ImGuiInputTextCallbackData;

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
    // Shows the whole word toggle disabled, for a backend that can't honor
    // it right now. Its setting is kept.
    bool whole_word_disabled = false;
    bool nav_buttons = true;
    bool match_counter = false;
    // Minimum width of the match counter, so the items after it don't shift
    // as the count grows. 0 sizes it to its text.
    float counter_width = 0.0f;
    bool close_button = false;
    const char* close_tooltip = "Close (Escape)";
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
    // Past search terms, most recent first, browsed with Up / Down while the
    // field has focus. The term is added when the user presses Enter or
    // leaves the field. Owned by the caller so it can be persisted. Null
    // disables history.
    std::vector<std::string>* history = nullptr;
  };

  // Most terms kept in Options::history.
  static constexpr size_t kMaxHistory = 50;

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

  // Replaces the text in the input field, truncated to fit.
  void SetTerm(const std::string& term);

 private:
  static int InputCallback(ImGuiInputTextCallbackData* data);

  // Replaces the field's text with the next older (|dir| > 0) or newer
  // (|dir| < 0) history entry.
  void StepHistory(ImGuiInputTextCallbackData* data, int dir);

  // Moves |term| to the front of the history.
  void AddToHistory(const char* term);

  static constexpr int kInputSize = 256;
  char input_[kInputSize] = {};

  std::string prev_term_;
  std::string counter_;
  bool focus_ = false;
  bool reload_ = false;  // See SetTerm().
  char reject_char_ = 0;
  int reject_frames_ = 0;

  // Options::history of the Update() in progress, for the input callback.
  std::vector<std::string>* history_ = nullptr;
  // Index of the history entry in the field, -1 while the user's own text is.
  int history_pos_ = -1;
  // The user's own text, put back when stepping past the newest entry.
  std::string history_draft_;
};

#endif  // GEL_UI_MODULES_SEARCH_BAR_H
