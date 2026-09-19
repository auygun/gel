// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/modules/search_bar.h"

#include <algorithm>
#include <cstdio>

#include "gel/ui/icons.h"
#include "gel/ui/utils.h"
#include "third_party/imgui/imgui/imgui.h"

namespace {

// Frames to keep the character filter armed after a shortcut focuses the
// field. ImGui queues input events, so the shortcut key can arrive a few
// frames after the key press that triggered the focus.
constexpr int kRejectFrames = 5;

void DefaultTooltip(const char* text) {
  auto flags = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_Stationary |
               ImGuiHoveredFlags_AllowWhenDisabled;
  if (ImGui::IsItemHovered(flags))
    ImGui::SetTooltip("%s", text);
}

}  // namespace

void SearchBar::FocusRejectingChar(char c) {
  focus_ = true;
  reject_char_ = c;
  reject_frames_ = kRejectFrames;
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
  bool entered = ImGui::InputText(
      widget_id("input"), input_, kInputSize, flags,
      [](ImGuiInputTextCallbackData* data) -> int {
        char rejected = *static_cast<const char*>(data->UserData);
        return data->EventChar == static_cast<ImWchar>(rejected) ? 1 : 0;
      },
      &reject_char_);
  result.input_active = ImGui::IsItemActive();
  result.input_deactivated = ImGui::IsItemDeactivated();
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
    float fill = std::clamp(opts.progress, 0.0f, 1.0f);
    float fill_w = width * fill;
    if (fill_w > 0.0f) {
      dl->AddRectFilled(input_pos,
                        ImVec2(input_pos.x + fill_w, input_pos.y + frame_h),
                        ImGui::GetColorU32(ImGuiCol_PlotHistogram), rounding,
                        fill >= 1.0f ? ImDrawFlags_RoundCornersAll
                                     : ImDrawFlags_RoundCornersLeft);
    }
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
    bool ww = match->whole_word;
    if (ToggleButton(widget_id("word"), ww, ImVec2(frame_h, frame_h)))
      match->whole_word = !ww;
    DrawWholeWordIcon();
    tooltip(ww ? "Whole word (click to toggle)"
               : "Partial word (click to toggle)");
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
    tooltip("Close (Escape)");
  }

  return result;
}
