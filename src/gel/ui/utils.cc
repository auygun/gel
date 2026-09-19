// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/utils.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "gel/ui/colored_line.h"
#include "gel/ui/style.h"
#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"
#include "third_party/kaliber/platform/platform.h"

void RenderColoredLine(const ColoredLine& line) {
  if (line.segments.empty())
    return;

  ImFont* font = ImGui::GetFont();
  float font_size = ImGui::GetFontSize();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  // Render each segment using ImDrawList::AddText with positions accumulated
  // via CalcTextSizeA. This avoids per-segment width rounding that
  // ImGui::TextUnformatted introduces (via CalcTextSize's ceil), which would
  // cause text positions to drift from the selection highlight measurements.
  float x_offset = 0;
  for (const auto& seg : line.segments) {
    uint32_t color = ResolveColor(seg.color);
    const char* text_begin = seg.text.c_str();
    const char* text_end = text_begin + seg.text.size();
    draw_list->AddText(font, font_size, ImVec2(pos.x + x_offset, pos.y), color,
                       text_begin, text_end);
    x_offset +=
        font->CalcTextSizeA(font_size, FLT_MAX, -1.0f, text_begin, text_end).x;
  }

  ImGui::Dummy(ImVec2(x_offset, ImGui::GetTextLineHeight()));
}

void ItemTooltip(const char* text, bool& any_hovered, bool tooltip_suppressed) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    any_hovered = true;
    if (!tooltip_suppressed) {
      bool showing = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                          ImGuiHoveredFlags_Stationary |
                                          ImGuiHoveredFlags_AllowWhenDisabled);
      if (showing)
        ImGui::SetTooltip("%s", text);
    }
  }
}

namespace {

// Mixes |c| toward |tint| by |t| and closes |ta| of the gap to opaque, so a
// translucent fill gains contrast on hover/press instead of only shifting hue.
ImVec4 Emphasize(const ImVec4& c, const ImVec4& tint, float t, float ta) {
  return ImVec4(c.x + (tint.x - c.x) * t, c.y + (tint.y - c.y) * t,
                c.z + (tint.z - c.z) * t, c.w + (1.0f - c.w) * ta);
}

}  // namespace

bool ToggleButton(const char* id, bool on, const ImVec2& size) {
  const ImVec4* colors = ImGui::GetStyle().Colors;
  // Step the feedback away from the window background: brighter on dark
  // themes, darker on light ones. Going the other way would sink the hovered
  // and pressed fills into the background instead of lifting them out of it.
  const ImVec4& bg = colors[ImGuiCol_WindowBg];
  bool dark = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z < 0.5f;
  ImVec4 tint = dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1);

  // Off starts from the plain button fill, on from the theme accent, and each
  // takes two steps of its own for hover and press. Themes that pick similar
  // colors for Button and ButtonActive would otherwise collapse those six
  // combinations into two or three distinguishable looks.
  ImVec4 fill = on ? colors[ImGuiCol_ButtonActive] : colors[ImGuiCol_Button];
  ImGui::PushStyleColor(ImGuiCol_Button, fill);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        Emphasize(fill, tint, 0.22f, 0.35f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        Emphasize(fill, tint, 0.42f, 0.65f));
  bool clicked = ImGui::Button(id, size);
  ImGui::PopStyleColor(3);

  // Outline the on state so it still reads as on while hovered or pressed,
  // and in layouts that draw no frame border at all.
  if (on) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ImGui::GetColorU32(Emphasize(fill, tint, 0.60f, 1.0f)),
        style.FrameRounding, ImDrawFlags_None,
        std::max(1.0f, style.FrameBorderSize));
  }
  return clicked;
}

void DrawMouseSpinner() {
  float scale = ImGui::GetStyle()._MainScale;
  ImVec2 mouse = ImGui::GetMousePos();
  float radius = 7.0f * scale;
  ImVec2 center(mouse.x + 22.0f * scale, mouse.y + 22.0f * scale);
  float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  fg->PathArcTo(center, radius, t, t + IM_PI * 1.5f, 12);
  fg->PathStroke(IM_COL32(200, 200, 200, 200), 0, 2.5f * scale);
}

void InputTextContextMenu(char* buf, size_t buf_size) {
  ImGuiID id = ImGui::GetItemID();

  // Capture cursor and selection state on right-click before the popup steals
  // focus.
  static ImGuiID saved_id = 0;
  static int saved_cursor = -1;
  static int saved_sel_start = -1;
  static int saved_sel_end = -1;

  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    saved_id = id;
    saved_cursor = -1;
    saved_sel_start = -1;
    saved_sel_end = -1;
    if (auto* state = ImGui::GetInputTextState(id)) {
      saved_cursor = state->GetCursorPos();
      if (state->HasSelection()) {
        saved_sel_start =
            std::min(state->GetSelectionStart(), state->GetSelectionEnd());
        saved_sel_end =
            std::max(state->GetSelectionStart(), state->GetSelectionEnd());
      }
    }
  }

  // Deferred focus: re-focus the input on the first frame after the popup
  // closes, then perform the pending action on the second frame once active.
  enum class DeferredAction { kNone, kSelectAll, kCursorAt };
  static ImGuiID deferred_target = 0;
  static DeferredAction deferred_action = DeferredAction::kNone;
  static int deferred_cursor_pos = 0;
  static int deferred_step = 0;

  if (deferred_target == id) {
    if (deferred_step == 0) {
      ImGui::SetKeyboardFocusHere(-1);
      deferred_step = 1;
    } else {
      if (auto* state = ImGui::GetInputTextState(id)) {
        if (deferred_action == DeferredAction::kSelectAll) {
          state->SelectAll();
        } else if (deferred_action == DeferredAction::kCursorAt) {
          state->WantReloadUserBuf = true;
          state->ReloadSelectionStart = deferred_cursor_pos;
          state->ReloadSelectionEnd = deferred_cursor_pos;
        }
        state->CursorAnimReset();
      }
      deferred_target = 0;
      deferred_action = DeferredAction::kNone;
      deferred_step = 0;
    }
  }

  // Middle-click paste from primary selection (Linux convention).
  if (ImGui::IsItemHovered() &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
    auto* platform =
        static_cast<eng::Platform*>(ImGui::GetIO().BackendPlatformUserData);
    if (platform) {
      const char* ps_text = platform->GetPrimarySelection();
      if (ps_text && ps_text[0]) {
        size_t ps_len = strlen(ps_text);
        size_t buf_len = strlen(buf);
        auto* state = ImGui::GetInputTextState(id);
        if (state && buf_len + ps_len < buf_size) {
          int cursor = state->GetCursorPos();
          if (cursor > static_cast<int>(buf_len))
            cursor = static_cast<int>(buf_len);
          memmove(buf + cursor + ps_len, buf + cursor, buf_len - cursor + 1);
          memcpy(buf + cursor, ps_text, ps_len);
          state->ReloadUserBufAndMoveToEnd();
        } else if (!state && ps_len < buf_size) {
          // Not active: overwrite buffer.
          size_t copy_len = std::min(ps_len, buf_size - 1);
          memcpy(buf, ps_text, copy_len);
          buf[copy_len] = '\0';
        }
      }
    }
  }

  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##ctx_menu_esc"),
                         ImGuiInputFlags_LockThisFrame);
      ImGui::CloseCurrentPopup();
    }

    bool has_text = buf[0] != '\0';
    bool has_sel = saved_id == id && saved_sel_start >= 0;

    if (ImGui::MenuItem("Select all", nullptr, false, has_text)) {
      deferred_target = id;
      deferred_action = DeferredAction::kSelectAll;
      deferred_step = 0;
    }
    if (ImGui::MenuItem("Copy", nullptr, false, has_text)) {
      if (has_sel) {
        int end = std::min(saved_sel_end, static_cast<int>(strlen(buf)));
        std::string s(buf + saved_sel_start, buf + end);
        ImGui::SetClipboardText(s.c_str());
      } else {
        ImGui::SetClipboardText(buf);
      }
    }
    if (ImGui::MenuItem("Cut", nullptr, false, has_text)) {
      int cut_cursor = 0;
      if (has_sel) {
        int end = std::min(saved_sel_end, static_cast<int>(strlen(buf)));
        std::string s(buf + saved_sel_start, buf + end);
        ImGui::SetClipboardText(s.c_str());
        memmove(buf + saved_sel_start, buf + end, strlen(buf + end) + 1);
        cut_cursor = saved_sel_start;
      } else {
        ImGui::SetClipboardText(buf);
        buf[0] = '\0';
      }
      deferred_target = id;
      deferred_action = DeferredAction::kCursorAt;
      deferred_cursor_pos = cut_cursor;
      deferred_step = 0;
    }
    const char* clip = ImGui::GetClipboardText();
    bool has_clip = clip && clip[0];
    if (ImGui::MenuItem("Paste", nullptr, false, has_clip) && clip) {
      size_t clip_len = strlen(clip);
      size_t buf_len = strlen(buf);
      int paste_end = 0;
      if (has_sel) {
        int end = std::min(saved_sel_end, static_cast<int>(buf_len));
        size_t tail_len = strlen(buf + end);
        if (saved_sel_start + clip_len + tail_len < buf_size) {
          memmove(buf + saved_sel_start + clip_len, buf + end, tail_len + 1);
          memcpy(buf + saved_sel_start, clip, clip_len);
          paste_end = saved_sel_start + static_cast<int>(clip_len);
        }
      } else {
        int cursor = saved_id == id && saved_cursor >= 0
                         ? std::min(saved_cursor, static_cast<int>(buf_len))
                         : static_cast<int>(buf_len);
        if (buf_len + clip_len < buf_size) {
          memmove(buf + cursor + clip_len, buf + cursor, buf_len - cursor + 1);
          memcpy(buf + cursor, clip, clip_len);
          paste_end = cursor + static_cast<int>(clip_len);
        }
      }
      deferred_target = id;
      deferred_action = DeferredAction::kCursorAt;
      deferred_cursor_pos = paste_end;
      deferred_step = 0;
    }
    ImGui::EndPopup();
  }
}
