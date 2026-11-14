// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_UTILS_H
#define GEL_UI_UTILS_H

#include <cstddef>

struct ColoredLine;
struct ImDrawList;
struct ImVec2;

// Renders a ColoredLine with per-segment colors resolved via ResolveColor.
void RenderColoredLine(const ColoredLine& line);

// Right-click context menu for InputText widgets (Select all, Copy, Cut,
// Paste). Call immediately after ImGui::InputText().
void InputTextContextMenu(char* buf, size_t buf_size);

// Shows a delayed tooltip for the last ImGui item. Tracks hover state via
// |any_hovered|. When |tooltip_suppressed| is true (e.g. after a click),
// the tooltip text is hidden but animation frames are still requested so
// the delay timer keeps ticking.
void ItemTooltip(const char* text, bool& any_hovered, bool tooltip_suppressed);

// Renders a two-state (toggle) button. Same geometry as ImGui::Button(), but
// the on state keeps its own fill and an accent outline, and both states get
// hover/press fills that step away from the window background, so on, off,
// hovered and pressed all stay apart in every theme. Draw the icon right
// after the call, as with ImGui::Button(). Returns true when clicked.
bool ToggleButton(const char* id, bool on, const ImVec2& size);

// Draws an animated spinner arc near the mouse cursor, for work the user is
// waiting on.
void DrawMouseSpinner();

// Draws the same spinner centered in the rect [min, max], for the toolbar's
// background git commands indicator.
void DrawBusySpinner(ImVec2 min, ImVec2 max);

// Fills the part of the rounded rect [min, max] left of min.x + |fill_w|, for
// progress bars inside rounded frames. A plain rect with only its own left
// corners rounded leaks past the rounded outline: ImGui shrinks the corner
// radius of a fill narrower than two radii, and the fill's square right edge
// sticks out of the right corners near the end. Clipping the rounded outline
// itself keeps the fill inside at any width.
void DrawRoundedFill(ImDrawList* dl,
                     ImVec2 min,
                     ImVec2 max,
                     float rounding,
                     float fill_w,
                     unsigned int color);

#endif  // GEL_UI_UTILS_H
