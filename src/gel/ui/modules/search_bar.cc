// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/search_bar.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "base/utf8.h"
#include "gel/ui/icons.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

namespace {

// Frames to keep the character filter armed after a shortcut focuses the
// field. ImGui queues input events, so the shortcut key can arrive a few
// frames after the key press that triggered the focus.
constexpr int kRejectFrames = 5;

// Hint in the focused, empty input field of a bar with a history. The arrows
// are U+2191 and U+2193 in UTF-8.
constexpr char kHistoryHint[] = "Find (\xe2\x86\x91\xe2\x86\x93 for history)";

void DefaultTooltip(const char* text) {
  auto flags = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary |
               ImGuiHoveredFlags_AllowWhenDisabled;
  if (ImGui::IsItemHovered(flags))
    ImGui::SetTooltip("%s", text);
}

}  // namespace

void SearchBar::SetTerm(const std::string& term) {
  snprintf(input_, kInputSize, "%s", term.c_str());
  // ImGui keeps its own copy of the text while the field is being edited.
  // Make it pick up the new text on the next Update().
  reload_ = true;
  history_pos_ = -1;
}

void SearchBar::FocusRejectingChar(char c) {
  focus_ = true;
  reject_char_ = c;
  reject_frames_ = kRejectFrames;
}

bool SearchBar::IsInputActive() const {
  return input_id_ != 0 && ImGui::GetActiveID() == input_id_;
}

int SearchBar::InputCallback(ImGuiInputTextCallbackData* data) {
  auto* self = static_cast<SearchBar*>(data->UserData);
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    return data->EventChar == static_cast<ImWchar>(self->reject_char_) ? 1 : 0;
  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    self->StepHistory(data, data->EventKey == ImGuiKey_UpArrow ? 1 : -1);
  return 0;
}

void SearchBar::StepHistory(ImGuiInputTextCallbackData* data, int dir) {
  // Leave modified arrows to their own shortcuts (Shift+Up / Shift+Down
  // navigate matches, Ctrl+Up / Ctrl+Down scroll the commit list).
  const ImGuiIO& io = ImGui::GetIO();
  if (!history_ || io.KeyShift || io.KeyCtrl || io.KeyAlt || io.KeySuper)
    return;

  if (history_pos_ < 0)
    history_draft_.assign(data->Buf, data->BufTextLen);
  // Skip the entry matching the user's own text, which would look like the
  // key did nothing (e.g. the term just searched for with Enter).
  const std::vector<std::string>& history = *history_;
  int size = static_cast<int>(history.size());
  int pos = history_pos_;
  do {
    pos += dir;
  } while (pos >= 0 && pos < size && history[pos] == history_draft_);
  if (pos >= size || (pos < 0 && history_pos_ < 0))
    return;  // Already at the oldest entry, or at the user's own text.
  pos = std::max(pos, -1);

  std::string_view text = pos < 0 ? history_draft_ : history[pos];
  text = text.substr(0, base::Utf8SafePrefixLength(text, data->BufSize - 1));
  history_pos_ = pos;
  data->DeleteChars(0, data->BufTextLen);
  data->InsertChars(0, text.data(), text.data() + text.size());
}

void SearchBar::AddToHistory(const char* term) {
  history_pos_ = -1;
  if (!history_ || !*term)
    return;
  std::vector<std::string>& history = *history_;
  std::erase(history, term);
  history.insert(history.begin(), term);
  if (history.size() > kMaxHistory)
    history.resize(kMaxHistory);
}

SearchBar::Result SearchBar::Update(const Options& opts,
                                    base::MatchOptions* match) {
  Result result;

  auto tooltip = [&opts](const char* text) {
    if (opts.tooltip)
      opts.tooltip(text);
    else
      DefaultTooltip(text);
  };

  char id[64];
  auto widget_id = [&id, &opts](const char* suffix) {
    snprintf(id, sizeof(id), "##%s_%s", opts.id, suffix);
    return id;
  };

  float frame_h = ImGui::GetFrameHeight();
  float width = opts.width > 0
                    ? opts.width
                    : ImGui::CalcTextSize("00000000000000000000000000").x +
                          ImGui::GetStyle().FramePadding.x * 2;
  ImGui::SetNextItemWidth(width);

  // The field's hint depends on whether it has focus. Its state as of the
  // last frame is the best available before drawing it, plus a pending focus
  // request so the hint doesn't lag a frame behind a shortcut.
  bool focused = focus_ || IsInputActive();
  const char* hint = focused ? (opts.history ? kHistoryHint : "Find")
                             : (opts.idle_hint ? opts.idle_hint : "");

  if (focus_) {
    ImGui::SetKeyboardFocusHere();
    focus_ = false;
  }

  // Embed search progress into the input's background. Split the draw list so
  // the fill lands behind the text ImGui draws for the field.
  ImVec2 input_pos = ImGui::GetCursorScreenPos();
  bool show_progress = opts.progress >= 0.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (show_progress) {
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  }

  ImGuiInputTextFlags flags =
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
  if (reject_frames_ > 0)
    flags |= ImGuiInputTextFlags_CallbackCharFilter;
  history_ = opts.history;
  if (history_)
    flags |= ImGuiInputTextFlags_CallbackHistory;
  bool entered =
      ImGui::InputTextWithHint(widget_id("input"), hint, input_, kInputSize,
                               flags, &SearchBar::InputCallback, this);
  input_id_ = ImGui::GetItemID();
  if (reload_) {
    reload_ = false;
    if (auto* state = ImGui::GetInputTextState(input_id_))
      state->ReloadUserBufAndSelectAll();
  }
  result.input_active = ImGui::IsItemActive();
  result.input_deactivated = ImGui::IsItemDeactivated();
  // Editing an entry pulled from the history makes it the user's own text.
  if (history_pos_ >= 0 &&
      (!history_ || history_pos_ >= static_cast<int>(history_->size()) ||
       (*history_)[history_pos_] != input_))
    history_pos_ = -1;
  if (entered || result.input_deactivated)
    AddToHistory(input_);
  if (reject_frames_ > 0)
    reject_frames_--;
  if (opts.on_input)
    opts.on_input(input_, kInputSize);

  if (show_progress) {
    ImGui::PopStyleColor(3);
    dl->ChannelsSetCurrent(0);
    float rounding = ImGui::GetStyle().FrameRounding;
    ImVec2 rect_max(input_pos.x + width, input_pos.y + frame_h);
    dl->AddRectFilled(input_pos, rect_max, ImGui::GetColorU32(ImGuiCol_FrameBg),
                      rounding);
    // Keep the fill inside the frame border, which ImGui draws over the field
    // afterwards (layouts with FrameBorderSize > 0). Under it, the border
    // would cut through the fill and leave slivers outside it.
    //
    // RenderFrame() strokes two rounded rects along the frame inset by half a
    // pixel, keeping the full rounding: the border (ImGuiCol_Border) and,
    // offset by (1, 1), its shadow (ImGuiCol_BorderShadow). Either may be the
    // visible one (the light style's border is transparent and its shadow
    // opaque), so the fill stays inside both, half a pixel clear of the inner
    // edge of each stroke for its anti-aliasing. That means one more pixel in
    // at the top and left, where the shadow lies further in.
    float border = ImGui::GetStyle().FrameBorderSize;
    ImVec2 fill_min = input_pos;
    ImVec2 fill_max = rect_max;
    float fill_rounding = rounding;
    if (border > 0.0f) {
      float inset = 0.5f + border * 0.5f + 0.5f;
      fill_min = ImVec2(input_pos.x + inset + 1.0f, input_pos.y + inset + 1.0f);
      fill_max = ImVec2(rect_max.x - inset, rect_max.y - inset);
      // Corner arcs centred where the strokes' arcs are (0.5 + rounding in
      // from the edge they follow), using the inner, shadow side's offset so
      // they stay clear of both strokes at every corner.
      fill_rounding = std::max(0.0f, 0.5f + rounding - inset);
    }
    float fill_w =
        (fill_max.x - fill_min.x) * std::clamp(opts.progress, 0.0f, 1.0f);
    DrawRoundedFill(dl, fill_min, fill_max, fill_rounding, fill_w,
                    ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    dl->ChannelsMerge();
  }

  if (opts.input_tooltip && !result.input_active)
    tooltip(opts.input_tooltip);

  if (prev_term_ != input_) {
    prev_term_ = input_;
    result.term_changed = true;
  }

  // Navigate matches with Enter / Shift+Enter. Single-line InputText validates
  // on Shift+Enter too (EnterReturnsTrue returns true for both), so pick the
  // direction from the Shift modifier rather than assuming Enter always means
  // "next match". Gating on |entered| ensures we only act when this search box
  // handled the key.
  bool shift_enter =
      ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter, true);
  if (entered && !empty()) {
    if (shift_enter)
      result.prev = true;
    else
      result.next = true;
    // Keep the field focused so Enter can be pressed repeatedly.
    focus_ = true;
  }

  if (opts.case_toggle && match) {
    ImGui::SameLine(0, 0);
    bool cs = match->case_sensitive;
    if (ToggleButton(widget_id("case"), cs, ImVec2(frame_h, frame_h)))
      match->case_sensitive = !cs;
    DrawCaseSensitiveIcon();
    tooltip(cs ? "Case sensitive (click to toggle)"
               : "Case insensitive (click to toggle)");
  }

  if (opts.whole_word_toggle && match) {
    ImGui::SameLine(0, 0);
    bool disabled = opts.whole_word_disabled;
    bool ww = match->whole_word && !disabled;
    ImGui::BeginDisabled(disabled);
    if (ToggleButton(widget_id("word"), ww, ImVec2(frame_h, frame_h)))
      match->whole_word = !ww;
    DrawWholeWordIcon();
    tooltip(disabled ? "Whole word is not available in this search mode"
            : ww     ? "Whole word (click to toggle)"
                     : "Partial word (click to toggle)");
    ImGui::EndDisabled();
  }

  if (opts.regex) {
    ImGui::SameLine(0, 0);
    bool disabled = opts.regex_disabled;
    bool re = *opts.regex && !disabled;
    ImGui::BeginDisabled(disabled);
    if (ToggleButton(widget_id("regex"), re, ImVec2(frame_h, frame_h)))
      *opts.regex = !re;
    DrawRegexIcon();
    tooltip(disabled ? "Regex is not available in this search mode"
            : re     ? "Regex (click to toggle)"
                     : "Literal string (click to toggle)");
    ImGui::EndDisabled();
  }

  bool nav_enabled = opts.nav_enable == NavEnable::kHasTerm
                         ? !empty()
                         : opts.matches.total > 0;
  if (opts.nav_buttons) {
    ImGui::SameLine(0, 0);
    float half_h = frame_h / 2;
    float btn_w =
        ImGui::CalcTextSize("W").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginDisabled(!nav_enabled);
    if (ImGui::Button(widget_id("up"), ImVec2(btn_w, half_h)))
      result.prev = true;
    DrawArrowUpIcon(nav_enabled);
    tooltip("Previous match");
    if (ImGui::Button(widget_id("down"), ImVec2(btn_w, half_h)))
      result.next = true;
    DrawArrowDownIcon(nav_enabled);
    tooltip("Next match");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
  }

  if (opts.arrow_shortcuts && nav_enabled && ImGui::GetIO().KeyShift) {
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
      result.prev = true;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
      result.next = true;
  }

  if (opts.match_counter) {
    const MatchState& m = opts.matches;
    if (empty()) {
      counter_.clear();
    } else if (m.total == 0) {
      counter_ = m.complete ? "No results" : "Searching...";
    } else {
      char buf[64];
      int current = m.current >= 0 ? m.current + 1 : 0;
      if (m.complete)
        snprintf(buf, sizeof(buf), "%d of %d", current, m.total);
      else
        snprintf(buf, sizeof(buf), "%d of %d+", current, m.total);
      counter_ = buf;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(counter_.c_str());
  }

  if (opts.close_button) {
    ImGui::SameLine();
    result.close_requested =
        ImGui::Button(widget_id("close"), ImVec2(frame_h, frame_h));
    DrawCloseIcon();
    tooltip(opts.close_tooltip);
  }

  return result;
}
