// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/title_bar.h"

#include <algorithm>
#include <cmath>

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/imgui/imgui/imgui_internal.h"

namespace {

// Sizes of the GNOME header bar, in units scaled with the UI.
constexpr float kHeight = 37.0f;
constexpr float kButtonSize = 24.0f;
constexpr float kButtonSpacing = 12.0f;
constexpr float kSideMargin = 7.0f;

// libadwaita's header bar colors.
struct Colors {
  ImU32 top;  // Background at the top, fading to |bottom|.
  ImU32 bottom;
  ImU32 highlight;  // One unit line along the top edge.
  ImU32 border;     // One unit line along the bottom edge.
  ImU32 title;
  ImU32 title_backdrop;  // Title while the window is not focused.
  ImU32 button_hovered;
  ImU32 button_pressed;
};

constexpr Colors kLight = {
    IM_COL32(0xEB, 0xEB, 0xEB, 0xFF), IM_COL32(0xE3, 0xE3, 0xE3, 0xFF),
    IM_COL32(0xF7, 0xF7, 0xF7, 0xFF), IM_COL32(0xD0, 0xD0, 0xD0, 0xFF),
    IM_COL32(0x2E, 0x2E, 0x2E, 0xFF), IM_COL32(0x8F, 0x8F, 0x8F, 0xFF),
    IM_COL32(0x00, 0x00, 0x00, 0x1A), IM_COL32(0x00, 0x00, 0x00, 0x33),
};

constexpr Colors kDark = {
    IM_COL32(0x2B, 0x2B, 0x2B, 0xFF), IM_COL32(0x26, 0x26, 0x26, 0xFF),
    IM_COL32(0x38, 0x38, 0x38, 0xFF), IM_COL32(0x1B, 0x1B, 0x1B, 0xFF),
    IM_COL32(0xEE, 0xEE, 0xEC, 0xFF), IM_COL32(0x91, 0x91, 0x91, 0xFF),
    IM_COL32(0xFF, 0xFF, 0xFF, 0x1A), IM_COL32(0xFF, 0xFF, 0xFF, 0x33),
};

float Scale() {
  return ImGui::GetStyle().FontScaleDpi;
}

bool Contains(const ImVec2& min, const ImVec2& max, const ImVec2& p) {
  return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
}

}  // namespace

TitleBar::TitleBar(Delegate& delegate) : delegate_(delegate) {}

float TitleBar::GetHeight() {
  // At least as tall as the title with some room, for large fonts.
  return std::round(
      std::max(kHeight * Scale(), ImGui::GetTextLineHeight() * 1.8f));
}

void TitleBar::Update() {
  const ImGuiIO& io = ImGui::GetIO();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = Scale();
  const float height = GetHeight();
  const ImVec2 min = viewport->Pos;
  const ImVec2 max(min.x + viewport->Size.x, min.y + height);
  const Colors& colors = delegate_.IsDarkMode() ? kDark : kLight;
  const bool focused = delegate_.IsWindowActive();

  // Behind every window: nothing else is drawn here, and modal dimming, which
  // a title bar does not get either, is drawn above.
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  dl->AddRectFilledMultiColor(min, max, colors.top, colors.top, colors.bottom,
                              colors.bottom);
  const float line = std::max(1.0f, std::floor(scale));
  dl->AddRectFilled(min, ImVec2(max.x, min.y + line), colors.highlight);
  dl->AddRectFilled(ImVec2(min.x, max.y - line), max, colors.border);

  // Buttons, right to left.
  struct ButtonRect {
    Button id;
    ImVec2 min, max;
  };
  ButtonRect buttons[3];
  int button_count = 0;
  const float size = std::round(kButtonSize * scale);
  const float y = std::round(min.y + (height - size) / 2);
  float x = max.x - std::round(kSideMargin * scale) - size;
  auto add_button = [&](Button id) {
    buttons[button_count++] = {id, ImVec2(x, y), ImVec2(x + size, y + size)};
    x -= size + std::round(kButtonSpacing * scale);
  };
  add_button(Button::kClose);
  if (delegate_.ShowMaximizeButton())
    add_button(Button::kMaximize);
  if (delegate_.ShowMinimizeButton())
    add_button(Button::kMinimize);
  // Left edge of the buttons, which the title stays clear of on both sides.
  const float buttons_width = max.x - (x + size);

  const ImVec2 mouse = io.MousePos;
  // Not under a window dragged over the title bar (the git console, say).
  // Left by a modal popup's dimming, which every other window is under.
  const bool over_bar =
      Contains(min, max, mouse) && !ImGui::GetCurrentContext()->HoveredWindow;
  Button hovered = Button::kNone;
  for (int i = 0; i < button_count; ++i) {
    if (Contains(buttons[i].min, buttons[i].max, mouse))
      hovered = buttons[i].id;
  }

  bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  if (clicked && over_bar) {
    pressed_ = hovered;
    if (hovered == Button::kNone) {
      // As GNOME's title bars do by default, maximize button or not.
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        delegate_.OnMaximizeToggleRequested();
      } else {
        delegate_.OnMoveRequested();
      }
    }
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && over_bar &&
      hovered == Button::kNone) {
    delegate_.OnWindowMenuRequested(static_cast<int>(mouse.x - min.x),
                                    static_cast<int>(mouse.y - min.y));
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (pressed_ != Button::kNone && pressed_ == hovered) {
      switch (pressed_) {
        case Button::kMinimize:
          delegate_.OnMinimizeRequested();
          break;
        case Button::kMaximize:
          delegate_.OnMaximizeToggleRequested();
          break;
        case Button::kClose:
          delegate_.OnCloseRequested();
          break;
        case Button::kNone:
          break;
      }
    }
    pressed_ = Button::kNone;
  }

  const ImU32 icon_color = focused ? colors.title : colors.title_backdrop;
  // Strokes are whole pixels, and icons sit on the pixel grid, so they stay
  // sharp. Sizes follow Adwaita's window button icons.
  const float t = std::max(1.0f, std::round(2.0f * scale));
  // An outlined rectangle of stroke |t|, as filled rectangles.
  auto outline = [&](float x0, float y0, float x1, float y1) {
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + t), icon_color);
    dl->AddRectFilled(ImVec2(x0, y1 - t), ImVec2(x1, y1), icon_color);
    dl->AddRectFilled(ImVec2(x0, y0 + t), ImVec2(x0 + t, y1 - t), icon_color);
    dl->AddRectFilled(ImVec2(x1 - t, y0 + t), ImVec2(x1, y1 - t), icon_color);
  };
  for (int i = 0; i < button_count; ++i) {
    const ButtonRect& b = buttons[i];
    const float cx = std::round((b.min.x + b.max.x) / 2);
    const float cy = std::round((b.min.y + b.max.y) / 2);
    if (b.id == hovered) {
      bool down = pressed_ == b.id && ImGui::IsMouseDown(ImGuiMouseButton_Left);
      dl->AddCircleFilled(ImVec2(cx, cy), size / 2,
                          down ? colors.button_pressed : colors.button_hovered,
                          32);
    }
    switch (b.id) {
      case Button::kClose: {
        // Two strokes across an 8 unit square, as filled quads: lines come out
        // thinner than asked.
        const float h = std::round(4.0f * scale);
        const float d = t / 2 * 0.7071f;
        const ImVec2 a(cx - h, cy - h);
        const ImVec2 e(cx + h, cy + h);
        dl->AddQuadFilled(ImVec2(a.x - d, a.y + d), ImVec2(a.x + d, a.y - d),
                          ImVec2(e.x + d, e.y - d), ImVec2(e.x - d, e.y + d),
                          icon_color);
        dl->AddQuadFilled(ImVec2(e.x - d, a.y - d), ImVec2(e.x + d, a.y + d),
                          ImVec2(a.x + d, e.y + d), ImVec2(a.x - d, e.y - d),
                          icon_color);
        break;
      }
      case Button::kMaximize: {
        const float h = std::round(5.0f * scale);
        if (delegate_.IsWindowMaximized()) {
          // Two overlapping squares, the one behind showing at the top right.
          const float s = std::round(7.0f * scale);
          const float x0 = cx - h, y1 = cy + h;
          outline(x0, y1 - s, x0 + s, y1);
          const float bx0 = cx + h - s, by0 = cy - h;
          dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(cx + h, by0 + t),
                            icon_color);
          dl->AddRectFilled(ImVec2(cx + h - t, by0 + t),
                            ImVec2(cx + h, by0 + s), icon_color);
          dl->AddRectFilled(ImVec2(bx0, by0 + t), ImVec2(bx0 + t, y1 - s),
                            icon_color);
          dl->AddRectFilled(ImVec2(x0 + s, by0 + s - t),
                            ImVec2(cx + h - t, by0 + s), icon_color);
        } else {
          outline(cx - h, cy - h, cx + h, cy + h);
        }
        break;
      }
      case Button::kMinimize: {
        const float h = std::round(5.0f * scale);
        dl->AddRectFilled(ImVec2(cx - h, cy + h - t), ImVec2(cx + h, cy + h),
                          icon_color);
        break;
      }
      case Button::kNone:
        break;
    }
  }

  // Title, centered in the bar unless that would run into the buttons, and
  // cut short with an ellipsis when it does not fit at all.
  const std::string& title = delegate_.GetWindowTitle();
  if (!title.empty()) {
    const float margin = buttons_width + std::round(kSideMargin * scale);
    const float left = min.x + margin;
    const float right = max.x - margin;
    const float text_height = ImGui::GetTextLineHeight();
    const float text_y = std::round(min.y + (height - text_height) / 2);
    if (right > left) {
      std::string text = title;
      float width = ImGui::CalcTextSize(text.c_str()).x;
      if (width > right - left) {
        // Drop whole UTF-8 characters until it fits with an ellipsis.
        const char* kEllipsis = "...";
        const float ellipsis = ImGui::CalcTextSize(kEllipsis).x;
        while (!text.empty() &&
               ImGui::CalcTextSize(text.c_str()).x + ellipsis > right - left) {
          // One whole character: its continuation bytes, then its first.
          auto is_continuation = [](char c) {
            return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
          };
          while (text.size() > 1 && is_continuation(text.back()))
            text.pop_back();
          text.pop_back();
        }
        text += kEllipsis;
        width = ImGui::CalcTextSize(text.c_str()).x;
      }
      const float text_x = std::round(std::clamp(
          min.x + (viewport->Size.x - width) / 2, left, right - width));
      const ImU32 color = focused ? colors.title : colors.title_backdrop;
      dl->PushClipRect(ImVec2(left, min.y), ImVec2(right, max.y), true);
      // Faux bold, the font having no bold face, as the GNOME title is.
      dl->AddText(ImVec2(text_x, text_y), color, text.c_str());
      dl->AddText(ImVec2(text_x + 1, text_y), color, text.c_str());
      dl->PopClipRect();
    }
  }
}
