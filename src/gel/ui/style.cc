// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/style.h"

#include "third_party/imgui/imgui/imgui.h"
#include "third_party/kaliber/base/log.h"

namespace {

// Blends `a` toward `b` by `t`, keeping the alpha of `a`.
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, a.w);
}

void ApplyDefaultLayout(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(8.00f, 8.00f);
  style.FramePadding = ImVec2(5.00f, 2.00f);
  style.CellPadding = ImVec2(6.00f, 6.00f);
  style.ItemSpacing = ImVec2(6.00f, 6.00f);
  style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
  style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
  style.IndentSpacing = 25;
  style.ScrollbarSize = 15;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 1;
  style.TabBorderSize = 1;
  style.WindowRounding = 7;
  style.ChildRounding = 4;
  style.FrameRounding = 3;
  style.PopupRounding = 4;
  style.ScrollbarRounding = 9;
  style.GrabRounding = 3;
  style.LogSliderDeadzone = 4;
  style.TabRounding = 4;
  style.HoverDelayNormal = 0.4f;
}

void ApplyCompactLayout(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(4.00f, 4.00f);
  style.FramePadding = ImVec2(4.00f, 1.00f);
  style.CellPadding = ImVec2(3.00f, 3.00f);
  style.ItemSpacing = ImVec2(4.00f, 3.00f);
  style.ItemInnerSpacing = ImVec2(4.00f, 4.00f);
  style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
  style.IndentSpacing = 20;
  style.ScrollbarSize = 12;
  style.GrabMinSize = 8;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 1;
  style.TabBorderSize = 1;
  style.WindowRounding = 4;
  style.ChildRounding = 2;
  style.FrameRounding = 2;
  style.PopupRounding = 3;
  style.ScrollbarRounding = 6;
  style.GrabRounding = 2;
  style.LogSliderDeadzone = 4;
  style.TabRounding = 2;
  style.HoverDelayNormal = 0.4f;
}

void ApplyComfortableLayout(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(12.00f, 12.00f);
  style.FramePadding = ImVec2(8.00f, 4.00f);
  style.CellPadding = ImVec2(8.00f, 8.00f);
  style.ItemSpacing = ImVec2(8.00f, 8.00f);
  style.ItemInnerSpacing = ImVec2(8.00f, 8.00f);
  style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
  style.IndentSpacing = 28;
  style.ScrollbarSize = 18;
  style.GrabMinSize = 14;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 1;
  style.TabBorderSize = 1;
  style.WindowRounding = 8;
  style.ChildRounding = 5;
  style.FrameRounding = 4;
  style.PopupRounding = 5;
  style.ScrollbarRounding = 10;
  style.GrabRounding = 4;
  style.LogSliderDeadzone = 4;
  style.TabRounding = 5;
  style.HoverDelayNormal = 0.4f;
}

void ApplyRoundedLayout(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(10.00f, 10.00f);
  style.FramePadding = ImVec2(6.00f, 3.00f);
  style.CellPadding = ImVec2(6.00f, 6.00f);
  style.ItemSpacing = ImVec2(6.00f, 6.00f);
  style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
  style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
  style.IndentSpacing = 25;
  style.ScrollbarSize = 16;
  style.GrabMinSize = 12;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.WindowRounding = 12;
  style.ChildRounding = 8;
  style.FrameRounding = 8;
  style.PopupRounding = 8;
  style.ScrollbarRounding = 12;
  style.GrabRounding = 8;
  style.LogSliderDeadzone = 4;
  style.TabRounding = 8;
  style.HoverDelayNormal = 0.4f;
}

void ApplyFlatLayout(ImGuiStyle& style) {
  style.WindowPadding = ImVec2(8.00f, 8.00f);
  style.FramePadding = ImVec2(5.00f, 2.00f);
  style.CellPadding = ImVec2(6.00f, 6.00f);
  style.ItemSpacing = ImVec2(6.00f, 6.00f);
  style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
  style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
  style.IndentSpacing = 25;
  style.ScrollbarSize = 14;
  style.GrabMinSize = 10;
  style.WindowBorderSize = 1;
  style.ChildBorderSize = 1;
  style.PopupBorderSize = 1;
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.WindowRounding = 0;
  style.ChildRounding = 0;
  style.FrameRounding = 0;
  style.PopupRounding = 0;
  style.ScrollbarRounding = 0;
  style.GrabRounding = 0;
  style.LogSliderDeadzone = 4;
  style.TabRounding = 0;
  style.HoverDelayNormal = 0.4f;
}

void ApplyDarkStyle() {
  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  colors[ImGuiCol_Border] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
}

void ApplyLightStyle() {
  ImVec4 accent(0.26f, 0.59f, 0.98f, 1.00f);
  ImVec4 title_bg(0.80f, 0.81f, 0.83f, 1.00f);

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.87f, 0.87f, 0.87f, 1.00f);
  colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.87f, 0.87f, 0.87f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.87f, 0.87f, 0.87f, 1.00f);
  colors[ImGuiCol_TitleBg] = title_bg;
  colors[ImGuiCol_TitleBgActive] = Mix(title_bg, accent, 0.55f);
  colors[ImGuiCol_TitleBgCollapsed] =
      ImVec4(title_bg.x, title_bg.y, title_bg.z, 0.50f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.76f, 0.84f, 0.95f, 0.54f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
  colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_Tab] = ImVec4(0.76f, 0.84f, 0.95f, 0.60f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
  colors[ImGuiCol_TabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

void ApplyCatppuccinMochaStyle() {
  // Catppuccin Mocha palette.
  ImVec4 base(0.12f, 0.12f, 0.18f, 1.00f);       // #1e1e2e
  ImVec4 mantle(0.09f, 0.09f, 0.15f, 1.00f);     // #181825
  ImVec4 crust(0.07f, 0.07f, 0.11f, 1.00f);      // #11111b
  ImVec4 surface0(0.19f, 0.20f, 0.27f, 1.00f);   // #313244
  ImVec4 surface1(0.27f, 0.28f, 0.35f, 1.00f);   // #45475a
  ImVec4 surface2(0.35f, 0.36f, 0.44f, 1.00f);   // #585b70
  ImVec4 overlay0(0.42f, 0.44f, 0.53f, 1.00f);   // #6c7086
  ImVec4 text(0.80f, 0.84f, 0.96f, 1.00f);       // #cdd6f4
  ImVec4 subtext0(0.65f, 0.68f, 0.78f, 1.00f);   // #a6adc8
  ImVec4 rosewater(0.96f, 0.88f, 0.86f, 1.00f);  // #f5e0dc
  ImVec4 lavender(0.71f, 0.75f, 1.00f, 1.00f);   // #b4befe
  ImVec4 mauve(0.80f, 0.65f, 0.97f, 1.00f);      // #cba6f7
  ImVec4 pink(0.96f, 0.76f, 0.91f,
              1.00f);  // #f5c2e7 (unused, kept for reference)
  ImVec4 red(0.95f, 0.55f, 0.66f, 1.00f);     // #f38ba8
  ImVec4 peach(0.98f, 0.70f, 0.53f, 1.00f);   // #fab387
  ImVec4 yellow(0.98f, 0.89f, 0.69f, 1.00f);  // #f9e2af
  ImVec4 green(0.65f, 0.89f, 0.63f, 1.00f);   // #a6e3a1
  ImVec4 teal(0.58f, 0.89f, 0.84f, 1.00f);    // #94e2d5
  ImVec4 blue(0.54f, 0.71f, 0.98f, 1.00f);    // #89b4fa
  (void)subtext0;
  (void)rosewater;
  (void)red;
  (void)yellow;
  (void)teal;
  (void)blue;
  (void)pink;

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = overlay0;
  colors[ImGuiCol_WindowBg] = base;
  colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(mantle.x, mantle.y, mantle.z, 0.95f);
  colors[ImGuiCol_Border] = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = crust;
  colors[ImGuiCol_TitleBgActive] = Mix(crust, mauve, 0.40f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(crust.x, crust.y, crust.z, 0.50f);
  colors[ImGuiCol_MenuBarBg] = mantle;
  colors[ImGuiCol_ScrollbarBg] = mantle;
  colors[ImGuiCol_ScrollbarGrab] = surface1;
  colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
  colors[ImGuiCol_ScrollbarGrabActive] = overlay0;
  colors[ImGuiCol_CheckMark] = green;
  colors[ImGuiCol_SliderGrab] = lavender;
  colors[ImGuiCol_SliderGrabActive] = mauve;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] = surface1;
  colors[ImGuiCol_ButtonActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_Header] = surface0;
  colors[ImGuiCol_HeaderHovered] = surface1;
  colors[ImGuiCol_HeaderActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.50f);
  colors[ImGuiCol_Separator] = surface1;
  colors[ImGuiCol_SeparatorHovered] = lavender;
  colors[ImGuiCol_SeparatorActive] = mauve;
  colors[ImGuiCol_ResizeGrip] =
      ImVec4(surface2.x, surface2.y, surface2.z, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.95f);
  colors[ImGuiCol_Tab] = surface0;
  colors[ImGuiCol_TabHovered] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_TabActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.80f);
  colors[ImGuiCol_TabUnfocused] = mantle;
  colors[ImGuiCol_TabUnfocusedActive] = surface0;
  colors[ImGuiCol_TableHeaderBg] = surface0;
  colors[ImGuiCol_TableBorderStrong] = surface2;
  colors[ImGuiCol_TableBorderLight] = surface1;
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.35f);
  colors[ImGuiCol_DragDropTarget] = peach;
  colors[ImGuiCol_NavHighlight] = lavender;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

void ApplyCatppuccinLatteStyle() {
  // Catppuccin Latte palette.
  ImVec4 base(0.94f, 0.95f, 0.96f, 1.00f);      // #eff1f5
  ImVec4 mantle(0.90f, 0.91f, 0.94f, 1.00f);    // #e6e9ef
  ImVec4 crust(0.86f, 0.88f, 0.91f, 1.00f);     // #dce0e8
  ImVec4 surface0(0.80f, 0.82f, 0.85f, 1.00f);  // #ccd0da
  ImVec4 surface1(0.74f, 0.75f, 0.80f, 1.00f);  // #bcc0cc
  ImVec4 surface2(0.67f, 0.69f, 0.75f, 1.00f);  // #acb0be
  ImVec4 overlay0(0.61f, 0.63f, 0.69f, 1.00f);  // #9ca0b0
  ImVec4 text(0.30f, 0.31f, 0.41f, 1.00f);      // #4c4f69
  ImVec4 subtext0(0.42f, 0.44f, 0.52f, 1.00f);  // #6c6f85
  ImVec4 lavender(0.45f, 0.53f, 0.99f, 1.00f);  // #7287fd
  ImVec4 mauve(0.53f, 0.22f, 0.94f, 1.00f);     // #8839ef
  ImVec4 pink(0.92f, 0.46f, 0.80f,
              1.00f);  // #ea76cb (unused, kept for reference)
  ImVec4 red(0.82f, 0.06f, 0.22f, 1.00f);     // #d20f39
  ImVec4 peach(1.00f, 0.39f, 0.04f, 1.00f);   // #fe640b
  ImVec4 yellow(0.87f, 0.56f, 0.11f, 1.00f);  // #df8e1d
  ImVec4 green(0.25f, 0.63f, 0.17f, 1.00f);   // #40a02b
  ImVec4 teal(0.09f, 0.57f, 0.60f, 1.00f);    // #179299
  ImVec4 blue(0.12f, 0.40f, 0.96f, 1.00f);    // #1e66f5
  (void)subtext0;
  (void)red;
  (void)yellow;
  (void)teal;
  (void)blue;
  (void)pink;

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = overlay0;
  colors[ImGuiCol_WindowBg] = base;
  colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(mantle.x, mantle.y, mantle.z, 0.95f);
  colors[ImGuiCol_Border] = surface0;
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = crust;
  // Latte's mauve is highly saturated; keep the focused tint light enough for
  // the dark title text to stay readable.
  colors[ImGuiCol_TitleBgActive] = Mix(crust, mauve, 0.32f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(crust.x, crust.y, crust.z, 0.50f);
  colors[ImGuiCol_MenuBarBg] = mantle;
  colors[ImGuiCol_ScrollbarBg] = mantle;
  colors[ImGuiCol_ScrollbarGrab] = surface1;
  colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
  colors[ImGuiCol_ScrollbarGrabActive] = overlay0;
  colors[ImGuiCol_CheckMark] = green;
  colors[ImGuiCol_SliderGrab] = lavender;
  colors[ImGuiCol_SliderGrabActive] = mauve;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] = surface1;
  colors[ImGuiCol_ButtonActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_Header] = surface0;
  colors[ImGuiCol_HeaderHovered] = surface1;
  colors[ImGuiCol_HeaderActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.50f);
  colors[ImGuiCol_Separator] = surface1;
  colors[ImGuiCol_SeparatorHovered] = lavender;
  colors[ImGuiCol_SeparatorActive] = mauve;
  colors[ImGuiCol_ResizeGrip] =
      ImVec4(surface2.x, surface2.y, surface2.z, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.95f);
  colors[ImGuiCol_Tab] = surface0;
  colors[ImGuiCol_TabHovered] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_TabActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.80f);
  colors[ImGuiCol_TabUnfocused] = mantle;
  colors[ImGuiCol_TabUnfocusedActive] = surface0;
  colors[ImGuiCol_TableHeaderBg] = surface0;
  colors[ImGuiCol_TableBorderStrong] = surface2;
  colors[ImGuiCol_TableBorderLight] = surface1;
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.02f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.35f);
  colors[ImGuiCol_DragDropTarget] = peach;
  colors[ImGuiCol_NavHighlight] = lavender;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

void ApplyCatppuccinFrappeStyle() {
  // Catppuccin Frappé palette.
  ImVec4 base(0.19f, 0.20f, 0.27f, 1.00f);      // #303446
  ImVec4 mantle(0.16f, 0.17f, 0.24f, 1.00f);    // #292c3c
  ImVec4 crust(0.14f, 0.15f, 0.20f, 1.00f);     // #232634
  ImVec4 surface0(0.25f, 0.27f, 0.35f, 1.00f);  // #414559
  ImVec4 surface1(0.32f, 0.34f, 0.43f, 1.00f);  // #51576d
  ImVec4 surface2(0.38f, 0.41f, 0.50f, 1.00f);  // #626880
  ImVec4 overlay0(0.45f, 0.47f, 0.58f, 1.00f);  // #737994
  ImVec4 text(0.78f, 0.82f, 0.96f, 1.00f);      // #c6d0f5
  ImVec4 subtext0(0.65f, 0.68f, 0.81f, 1.00f);  // #a5adce
  ImVec4 lavender(0.73f, 0.73f, 0.95f, 1.00f);  // #babbf1
  ImVec4 mauve(0.79f, 0.62f, 0.90f, 1.00f);     // #ca9ee6
  ImVec4 pink(0.96f, 0.72f, 0.89f,
              1.00f);  // #f4b8e4 (unused, kept for reference)
  ImVec4 red(0.91f, 0.51f, 0.52f, 1.00f);     // #e78284
  ImVec4 peach(0.94f, 0.62f, 0.46f, 1.00f);   // #ef9f76
  ImVec4 yellow(0.90f, 0.78f, 0.56f, 1.00f);  // #e5c890
  ImVec4 green(0.65f, 0.82f, 0.54f, 1.00f);   // #a6d189
  ImVec4 teal(0.51f, 0.78f, 0.75f, 1.00f);    // #81c8be
  ImVec4 blue(0.55f, 0.67f, 0.93f, 1.00f);    // #8caaee
  (void)subtext0;
  (void)red;
  (void)yellow;
  (void)teal;
  (void)blue;
  (void)pink;

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = overlay0;
  colors[ImGuiCol_WindowBg] = base;
  colors[ImGuiCol_ChildBg] = ImVec4(0.21f, 0.22f, 0.30f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(mantle.x, mantle.y, mantle.z, 0.95f);
  colors[ImGuiCol_Border] = ImVec4(0.21f, 0.22f, 0.30f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = crust;
  colors[ImGuiCol_TitleBgActive] = Mix(crust, mauve, 0.40f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(crust.x, crust.y, crust.z, 0.50f);
  colors[ImGuiCol_MenuBarBg] = mantle;
  colors[ImGuiCol_ScrollbarBg] = mantle;
  colors[ImGuiCol_ScrollbarGrab] = surface1;
  colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
  colors[ImGuiCol_ScrollbarGrabActive] = overlay0;
  colors[ImGuiCol_CheckMark] = green;
  colors[ImGuiCol_SliderGrab] = lavender;
  colors[ImGuiCol_SliderGrabActive] = mauve;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] = surface1;
  colors[ImGuiCol_ButtonActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_Header] = surface0;
  colors[ImGuiCol_HeaderHovered] = surface1;
  colors[ImGuiCol_HeaderActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.50f);
  colors[ImGuiCol_Separator] = surface1;
  colors[ImGuiCol_SeparatorHovered] = lavender;
  colors[ImGuiCol_SeparatorActive] = mauve;
  colors[ImGuiCol_ResizeGrip] =
      ImVec4(surface2.x, surface2.y, surface2.z, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.95f);
  colors[ImGuiCol_Tab] = surface0;
  colors[ImGuiCol_TabHovered] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_TabActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.80f);
  colors[ImGuiCol_TabUnfocused] = mantle;
  colors[ImGuiCol_TabUnfocusedActive] = surface0;
  colors[ImGuiCol_TableHeaderBg] = surface0;
  colors[ImGuiCol_TableBorderStrong] = surface2;
  colors[ImGuiCol_TableBorderLight] = surface1;
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.35f);
  colors[ImGuiCol_DragDropTarget] = peach;
  colors[ImGuiCol_NavHighlight] = lavender;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

void ApplyCatppuccinMacchiatoStyle() {
  // Catppuccin Macchiato palette.
  ImVec4 base(0.14f, 0.15f, 0.23f, 1.00f);      // #24273a
  ImVec4 mantle(0.12f, 0.13f, 0.19f, 1.00f);    // #1e2030
  ImVec4 crust(0.09f, 0.10f, 0.15f, 1.00f);     // #181926
  ImVec4 surface0(0.21f, 0.23f, 0.31f, 1.00f);  // #363a4f
  ImVec4 surface1(0.29f, 0.30f, 0.39f, 1.00f);  // #494d64
  ImVec4 surface2(0.36f, 0.38f, 0.47f, 1.00f);  // #5b6078
  ImVec4 overlay0(0.43f, 0.45f, 0.55f, 1.00f);  // #6e738d
  ImVec4 text(0.79f, 0.83f, 0.96f, 1.00f);      // #cad3f5
  ImVec4 subtext0(0.65f, 0.68f, 0.80f, 1.00f);  // #a5adcb
  ImVec4 lavender(0.72f, 0.74f, 0.97f, 1.00f);  // #b7bdf8
  ImVec4 mauve(0.78f, 0.63f, 0.96f, 1.00f);     // #c6a0f6
  ImVec4 pink(0.96f, 0.74f, 0.90f,
              1.00f);  // #f5bde6 (unused, kept for reference)
  ImVec4 red(0.93f, 0.53f, 0.59f, 1.00f);     // #ed8796
  ImVec4 peach(0.96f, 0.66f, 0.50f, 1.00f);   // #f5a97f
  ImVec4 yellow(0.93f, 0.83f, 0.62f, 1.00f);  // #eed49f
  ImVec4 green(0.65f, 0.85f, 0.58f, 1.00f);   // #a6da95
  ImVec4 teal(0.55f, 0.84f, 0.79f, 1.00f);    // #8bd5ca
  ImVec4 blue(0.54f, 0.68f, 0.96f, 1.00f);    // #8aadf4
  (void)subtext0;
  (void)red;
  (void)yellow;
  (void)teal;
  (void)blue;
  (void)pink;

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = overlay0;
  colors[ImGuiCol_WindowBg] = base;
  colors[ImGuiCol_ChildBg] = ImVec4(0.16f, 0.17f, 0.25f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(mantle.x, mantle.y, mantle.z, 0.95f);
  colors[ImGuiCol_Border] = ImVec4(0.16f, 0.17f, 0.25f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = crust;
  colors[ImGuiCol_TitleBgActive] = Mix(crust, mauve, 0.40f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(crust.x, crust.y, crust.z, 0.50f);
  colors[ImGuiCol_MenuBarBg] = mantle;
  colors[ImGuiCol_ScrollbarBg] = mantle;
  colors[ImGuiCol_ScrollbarGrab] = surface1;
  colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
  colors[ImGuiCol_ScrollbarGrabActive] = overlay0;
  colors[ImGuiCol_CheckMark] = green;
  colors[ImGuiCol_SliderGrab] = lavender;
  colors[ImGuiCol_SliderGrabActive] = mauve;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] = surface1;
  colors[ImGuiCol_ButtonActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_Header] = surface0;
  colors[ImGuiCol_HeaderHovered] = surface1;
  colors[ImGuiCol_HeaderActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.50f);
  colors[ImGuiCol_Separator] = surface1;
  colors[ImGuiCol_SeparatorHovered] = lavender;
  colors[ImGuiCol_SeparatorActive] = mauve;
  colors[ImGuiCol_ResizeGrip] =
      ImVec4(surface2.x, surface2.y, surface2.z, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.67f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.95f);
  colors[ImGuiCol_Tab] = surface0;
  colors[ImGuiCol_TabHovered] = ImVec4(mauve.x, mauve.y, mauve.z, 0.60f);
  colors[ImGuiCol_TabActive] = ImVec4(mauve.x, mauve.y, mauve.z, 0.80f);
  colors[ImGuiCol_TabUnfocused] = mantle;
  colors[ImGuiCol_TabUnfocusedActive] = surface0;
  colors[ImGuiCol_TableHeaderBg] = surface0;
  colors[ImGuiCol_TableBorderStrong] = surface2;
  colors[ImGuiCol_TableBorderLight] = surface1;
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(lavender.x, lavender.y, lavender.z, 0.35f);
  colors[ImGuiCol_DragDropTarget] = peach;
  colors[ImGuiCol_NavHighlight] = lavender;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

void ApplyRedLightDistrictStyle() {
  // Neon signs at night: dark backdrop with vivid, varied neon colors.
  ImVec4 bg(0.06f, 0.02f, 0.04f, 1.00f);         // Near-black with purple tint
  ImVec4 child_bg(0.10f, 0.03f, 0.06f, 1.00f);   // Slightly lighter
  ImVec4 popup_bg(0.08f, 0.02f, 0.05f, 0.95f);   // Dark popup
  ImVec4 surface0(0.18f, 0.05f, 0.10f, 1.00f);   // Dark surface
  ImVec4 surface1(0.25f, 0.07f, 0.14f, 1.00f);   // Mid surface
  ImVec4 surface2(0.32f, 0.10f, 0.18f, 1.00f);   // Lighter surface
  ImVec4 overlay0(0.50f, 0.25f, 0.35f, 1.00f);   // Muted mauve
  ImVec4 text(0.95f, 0.88f, 0.90f, 1.00f);       // Warm white-pink
  ImVec4 neon_red(0.95f, 0.10f, 0.20f, 1.00f);   // Neon red sign
  ImVec4 neon_pink(1.00f, 0.20f, 0.60f, 1.00f);  // Hot pink neon
  ImVec4 neon_magenta(0.85f, 0.15f, 0.80f, 1.00f);  // Electric magenta
  ImVec4 neon_purple(0.65f, 0.20f, 0.95f, 1.00f);   // Vivid purple
  ImVec4 neon_blue(0.20f, 0.40f, 1.00f, 1.00f);     // Electric blue
  ImVec4 neon_cyan(0.00f, 0.90f, 0.90f, 1.00f);     // Cyan glow
  ImVec4 neon_green(0.20f, 1.00f, 0.40f, 1.00f);    // Neon green
  ImVec4 neon_yellow(1.00f, 0.95f, 0.15f, 1.00f);   // Yellow neon
  ImVec4 neon_orange(1.00f, 0.50f, 0.05f, 1.00f);   // Orange neon

  ImVec4* colors = ImGui::GetStyle().Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = overlay0;
  colors[ImGuiCol_WindowBg] = bg;
  colors[ImGuiCol_ChildBg] = child_bg;
  colors[ImGuiCol_PopupBg] = popup_bg;
  colors[ImGuiCol_Border] = ImVec4(0.30f, 0.05f, 0.15f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.01f, 0.03f, 1.00f);
  colors[ImGuiCol_TitleBgActive] =
      Mix(ImVec4(0.05f, 0.01f, 0.03f, 1.00f), neon_magenta, 0.40f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.01f, 0.03f, 0.50f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.02f, 0.06f, 1.00f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.02f, 0.05f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab] =
      ImVec4(neon_purple.x, neon_purple.y, neon_purple.z, 0.50f);
  colors[ImGuiCol_ScrollbarGrabHovered] = neon_magenta;
  colors[ImGuiCol_ScrollbarGrabActive] = neon_pink;
  colors[ImGuiCol_CheckMark] = neon_green;
  colors[ImGuiCol_SliderGrab] = neon_cyan;
  colors[ImGuiCol_SliderGrabActive] = neon_blue;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] =
      ImVec4(neon_magenta.x, neon_magenta.y, neon_magenta.z, 0.35f);
  colors[ImGuiCol_ButtonActive] =
      ImVec4(neon_pink.x, neon_pink.y, neon_pink.z, 0.60f);
  colors[ImGuiCol_Header] =
      ImVec4(neon_magenta.x, neon_magenta.y, neon_magenta.z, 0.35f);
  colors[ImGuiCol_HeaderHovered] =
      ImVec4(neon_purple.x, neon_purple.y, neon_purple.z, 0.45f);
  colors[ImGuiCol_HeaderActive] =
      ImVec4(neon_red.x, neon_red.y, neon_red.z, 0.55f);
  colors[ImGuiCol_Separator] =
      ImVec4(neon_magenta.x, neon_magenta.y, neon_magenta.z, 0.30f);
  colors[ImGuiCol_SeparatorHovered] = neon_pink;
  colors[ImGuiCol_SeparatorActive] = neon_red;
  colors[ImGuiCol_ResizeGrip] =
      ImVec4(neon_blue.x, neon_blue.y, neon_blue.z, 0.20f);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(neon_cyan.x, neon_cyan.y, neon_cyan.z, 0.67f);
  colors[ImGuiCol_ResizeGripActive] =
      ImVec4(neon_green.x, neon_green.y, neon_green.z, 0.95f);
  colors[ImGuiCol_Tab] = surface0;
  colors[ImGuiCol_TabHovered] =
      ImVec4(neon_pink.x, neon_pink.y, neon_pink.z, 0.60f);
  colors[ImGuiCol_TabActive] =
      ImVec4(neon_magenta.x, neon_magenta.y, neon_magenta.z, 0.70f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.02f, 0.05f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = surface0;
  colors[ImGuiCol_TableHeaderBg] =
      ImVec4(neon_purple.x, neon_purple.y, neon_purple.z, 0.15f);
  colors[ImGuiCol_TableBorderStrong] =
      ImVec4(neon_magenta.x, neon_magenta.y, neon_magenta.z, 0.30f);
  colors[ImGuiCol_TableBorderLight] =
      ImVec4(neon_purple.x, neon_purple.y, neon_purple.z, 0.20f);
  colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 0.00f, 0.20f, 0.03f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(neon_blue.x, neon_blue.y, neon_blue.z, 0.35f);
  colors[ImGuiCol_PlotHistogram] = neon_yellow;
  colors[ImGuiCol_DragDropTarget] = neon_yellow;
  colors[ImGuiCol_NavHighlight] = neon_orange;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
}

}  // namespace

// Tracks the resolved style (kSystem resolves to kDark or kLight).
Style active_style = Style::kDark;

// Tracks the active layout for separator thickness queries.
Layout active_layout = Layout::kDefault;

// ANSI palette for the active style.
AnsiPalette active_ansi_palette;

// Syntax highlighting palette for the active style.
SyntaxPalette active_syntax_palette;

const char* GetStyleName(Style style) {
  switch (style) {
    case Style::kSystem:
      return "System default";
    case Style::kDark:
      return "Dark";
    case Style::kLight:
      return "Light";
    case Style::kCatppuccinMocha:
      return "Catppuccin Mocha";
    case Style::kCatppuccinLatte:
      return "Catppuccin Latte";
    case Style::kCatppuccinFrappe:
      return "Catppuccin Frappé";
    case Style::kCatppuccinMacchiato:
      return "Catppuccin Macchiato";
    case Style::kRedLightDistrict:
      return "Red Light District";
    case Style::kCount:
      break;
  }
  return "System";
}

const char* GetLayoutName(Layout layout) {
  switch (layout) {
    case Layout::kDefault:
      return "Default";
    case Layout::kCompact:
      return "Compact";
    case Layout::kComfortable:
      return "Comfortable";
    case Layout::kRounded:
      return "Rounded";
    case Layout::kFlat:
      return "Flat";
    case Layout::kCount:
      break;
  }
  return "Default";
}

bool IsLightStyle(Style style) {
  switch (style) {
    case Style::kLight:
    case Style::kCatppuccinLatte:
      return true;
    default:
      return false;
  }
}

void ApplyStyle(Style style, Layout layout, bool os_dark_mode) {
  if (style == Style::kSystem)
    style = os_dark_mode ? Style::kDark : Style::kLight;
  active_style = style;
  active_layout = layout;

  // Reset to ImGui defaults based on color brightness.
  bool is_light = IsLightStyle(style);
  if (is_light)
    ImGui::StyleColorsLight();
  else
    ImGui::StyleColorsDark();

  switch (layout) {
    case Layout::kDefault:
      ApplyDefaultLayout(ImGui::GetStyle());
      break;
    case Layout::kCompact:
      ApplyCompactLayout(ImGui::GetStyle());
      break;
    case Layout::kComfortable:
      ApplyComfortableLayout(ImGui::GetStyle());
      break;
    case Layout::kRounded:
      ApplyRoundedLayout(ImGui::GetStyle());
      break;
    case Layout::kFlat:
      ApplyFlatLayout(ImGui::GetStyle());
      break;
    case Layout::kCount:
      NOTREACHED();
      break;
  }

  // Apply color theme.
  switch (style) {
    case Style::kDark:
      ApplyDarkStyle();
      // VS Code Dark+ terminal ANSI colors.
      active_ansi_palette = {
          {0xFF000000, 0xFF3131CD, 0xFF79BC0D, 0xFF10E5E5, 0xFFC87224,
           0xFFBC3FBC, 0xFFCDA811, 0xFFE5E5E5},
          {0xFF666666, 0xFF4C4CF1, 0xFF8BD123, 0xFF43F5F5, 0xFFEA8E3B,
           0xFFD670D6, 0xFFDBB829, 0xFFE5E5E5},
      };
      // VS Code Dark+ syntax colors.
      active_syntax_palette = {
          0xFFD69C56,                   // keyword: blue
          0xFF7AADCE,                   // string: orange
          0xFF57A64A,                   // comment: green
          0xFFA5CEB8,                   // number: light green
          0xFFD69C56,                   // preprocessor: blue
          0xFF85C586,                   // type: teal
          IM_COL32(0, 100, 0, 100),     // added_bg
          IM_COL32(100, 0, 0, 100),     // removed_bg
          IM_COL32(100, 100, 0, 100),   // tag_bg
          IM_COL32(0, 60, 140, 100),    // stash_bg
          IM_COL32(55, 55, 60, 255),    // file_header_bg
          IM_COL32(255, 165, 0, 100),   // search_match_bg
          IM_COL32(255, 165, 0, 200),   // search_current_bg
          IM_COL32(64, 200, 174, 128),  // current_header_bg
          IM_COL32(64, 200, 174, 51),   // current_content_bg
          IM_COL32(64, 166, 255, 128),  // incoming_header_bg
          IM_COL32(64, 166, 255, 51),   // incoming_content_bg
      };
      break;
    case Style::kLight:
      ApplyLightStyle();
      // macOS Terminal Basic (light) profile ANSI colors.
      active_ansi_palette = {
          {0xFF000000, 0xFF000099, 0xFF00A600, 0xFF009999, 0xFFB20000,
           0xFFB200B2, 0xFFB2A600, 0xFFBFBFBF},
          {0xFF666666, 0xFF0000E5, 0xFF00D900, 0xFF00E5E5, 0xFFFF0000,
           0xFFE500E5, 0xFFE5E500, 0xFFE5E5E5},
      };
      // VS Code Light+ syntax colors.
      active_syntax_palette = {
          0xFFAF0000,                    // keyword: blue
          0xFF1515A3,                    // string: red-brown
          0xFF008000,                    // comment: green
          0xFF3700AC,                    // number: dark red
          0xFFAF0000,                    // preprocessor: blue
          0xFF7C5D26,                    // type: dark cyan
          IM_COL32(0, 120, 0, 40),       // added_bg
          IM_COL32(120, 0, 0, 40),       // removed_bg
          IM_COL32(120, 120, 0, 40),     // tag_bg
          IM_COL32(0, 60, 160, 40),      // stash_bg
          IM_COL32(204, 208, 218, 255),  // file_header_bg: surface0
          IM_COL32(255, 165, 0, 100),    // search_match_bg
          IM_COL32(255, 165, 0, 200),    // search_current_bg
          IM_COL32(64, 200, 174, 128),   // current_header_bg
          IM_COL32(64, 200, 174, 51),    // current_content_bg
          IM_COL32(64, 166, 255, 128),   // incoming_header_bg
          IM_COL32(64, 166, 255, 51),    // incoming_content_bg
      };
      break;
    case Style::kCatppuccinMocha:
      ApplyCatppuccinMochaStyle();
      // Official Catppuccin ANSI palette for Mocha.
      // Normal: Surface1, Red, Green, Yellow, Blue, Pink, Teal, Subtext0
      // Bright: Surface2, Red, Green, Yellow, Blue, Pink, Teal, Subtext1
      active_ansi_palette = {
          {0xFF5A4745, 0xFFA88BF3, 0xFFA1E3A6, 0xFFAFE2F9, 0xFFFAB489,
           0xFFE7C2F5, 0xFFD5E294, 0xFFC8ADA6},
          {0xFF705B58, 0xFFA88BF3, 0xFFA1E3A6, 0xFFAFE2F9, 0xFFFAB489,
           0xFFE7C2F5, 0xFFD5E294, 0xFFDEC2BA},
      };
      // Catppuccin Mocha language defaults.
      active_syntax_palette = {
          0xFFF7A6CB,                    // keyword: mauve
          0xFFA1E3A6,                    // string: green
          0xFFB29993,                    // comment: overlay2
          0xFF87B3FA,                    // number: peach
          0xFFDCE0F5,                    // preprocessor: rosewater
          0xFFAFE2F9,                    // type: yellow
          IM_COL32(166, 227, 161, 51),   // added_bg: green 20%
          IM_COL32(243, 139, 168, 51),   // removed_bg: red 20%
          IM_COL32(249, 226, 175, 51),   // tag_bg: yellow 20%
          IM_COL32(137, 180, 250, 51),   // stash_bg: blue 20%
          IM_COL32(49, 50, 68, 255),     // file_header_bg: surface0
          IM_COL32(250, 179, 135, 100),  // search_match_bg: peach
          IM_COL32(250, 179, 135, 200),  // search_current_bg: peach
          IM_COL32(64, 200, 174, 128),   // current_header_bg
          IM_COL32(64, 200, 174, 51),    // current_content_bg
          IM_COL32(64, 166, 255, 128),   // incoming_header_bg
          IM_COL32(64, 166, 255, 51),    // incoming_content_bg
      };
      break;
    case Style::kCatppuccinLatte:
      ApplyCatppuccinLatteStyle();
      // Official Catppuccin ANSI palette for Latte.
      // Normal: Subtext1, Red, Green, Yellow, Blue, Pink, Teal, Surface2
      // Bright: Subtext0, Red, Green, Yellow, Blue, Pink, Teal, Surface1
      active_ansi_palette = {
          {0xFF775F5C, 0xFF390FD2, 0xFF2BA040, 0xFF1D8EDF, 0xFFF5661E,
           0xFFCB76EA, 0xFF999217, 0xFFBEB0AC},
          {0xFF856F6C, 0xFF390FD2, 0xFF2BA040, 0xFF1D8EDF, 0xFFF5661E,
           0xFFCB76EA, 0xFF999217, 0xFFCCC0BC},
      };
      // Catppuccin Latte language defaults.
      active_syntax_palette = {
          0xFFEF3988,                    // keyword: mauve
          0xFF2BA040,                    // string: green
          0xFF937F7C,                    // comment: overlay2
          0xFF0B64FE,                    // number: peach
          0xFF788ADC,                    // preprocessor: rosewater
          0xFF1D8EDF,                    // type: yellow
          IM_COL32(64, 160, 43, 51),     // added_bg: green 20%
          IM_COL32(210, 15, 57, 51),     // removed_bg: red 20%
          IM_COL32(223, 142, 29, 51),    // tag_bg: yellow 20%
          IM_COL32(30, 102, 245, 51),    // stash_bg: blue 20%
          IM_COL32(204, 208, 218, 255),  // file_header_bg: surface0
          IM_COL32(223, 142, 29, 100),   // search_match_bg: peach
          IM_COL32(223, 142, 29, 200),   // search_current_bg: peach
          IM_COL32(64, 200, 174, 128),   // current_header_bg
          IM_COL32(64, 200, 174, 51),    // current_content_bg
          IM_COL32(64, 166, 255, 128),   // incoming_header_bg
          IM_COL32(64, 166, 255, 51),    // incoming_content_bg
      };
      break;
    case Style::kCatppuccinFrappe:
      ApplyCatppuccinFrappeStyle();
      // Official Catppuccin ANSI palette for Frappé.
      // Normal: Surface1, Red, Green, Yellow, Blue, Pink, Teal, Subtext0
      // Bright: Surface2, Red, Green, Yellow, Blue, Pink, Teal, Subtext1
      active_ansi_palette = {
          {0xFF6D5751, 0xFF8482E7, 0xFF89D1A6, 0xFF90C8E5, 0xFFEEAA8C,
           0xFFE4B8F4, 0xFFBEC881, 0xFFCEADA5},
          {0xFF806862, 0xFF8482E7, 0xFF89D1A6, 0xFF90C8E5, 0xFFEEAA8C,
           0xFFE4B8F4, 0xFFBEC881, 0xFFE2BFB5},
      };
      // Catppuccin Frappe language defaults.
      active_syntax_palette = {
          0xFFE69ECA,                    // keyword: mauve
          0xFF89D1A6,                    // string: green
          0xFFBB9C94,                    // comment: overlay2
          0xFF769FEF,                    // number: peach
          0xFFCFD5F2,                    // preprocessor: rosewater
          0xFF90C8E5,                    // type: yellow
          IM_COL32(166, 209, 137, 51),   // added_bg: green 20%
          IM_COL32(231, 130, 132, 51),   // removed_bg: red 20%
          IM_COL32(229, 200, 144, 51),   // tag_bg: yellow 20%
          IM_COL32(140, 170, 238, 51),   // stash_bg: blue 20%
          IM_COL32(65, 69, 89, 255),     // file_header_bg: surface0
          IM_COL32(239, 159, 118, 100),  // search_match_bg: peach
          IM_COL32(239, 159, 118, 200),  // search_current_bg: peach
          IM_COL32(64, 200, 174, 128),   // current_header_bg
          IM_COL32(64, 200, 174, 51),    // current_content_bg
          IM_COL32(64, 166, 255, 128),   // incoming_header_bg
          IM_COL32(64, 166, 255, 51),    // incoming_content_bg
      };
      break;
    case Style::kCatppuccinMacchiato:
      ApplyCatppuccinMacchiatoStyle();
      // Official Catppuccin ANSI palette for Macchiato.
      // Normal: Surface1, Red, Green, Yellow, Blue, Pink, Teal, Subtext0
      // Bright: Surface2, Red, Green, Yellow, Blue, Pink, Teal, Subtext1
      active_ansi_palette = {
          {0xFF644D49, 0xFF9687ED, 0xFF95DAA6, 0xFF9FD4EE, 0xFFF4AD8A,
           0xFFE6BDF5, 0xFFCAD58B, 0xFFCBADA5},
          {0xFF78605B, 0xFF9687ED, 0xFF95DAA6, 0xFF9FD4EE, 0xFFF4AD8A,
           0xFFE6BDF5, 0xFFCAD58B, 0xFFE0C0B8},
      };
      // Catppuccin Macchiato language defaults.
      active_syntax_palette = {
          0xFFF6A0C6,                    // keyword: mauve
          0xFF95DAA6,                    // string: green
          0xFFB79A93,                    // comment: overlay2
          0xFF7FA9F5,                    // number: peach
          0xFFD6DBF4,                    // preprocessor: rosewater
          0xFF9FD4EE,                    // type: yellow
          IM_COL32(166, 218, 149, 51),   // added_bg: green 20%
          IM_COL32(237, 135, 150, 51),   // removed_bg: red 20%
          IM_COL32(238, 212, 159, 51),   // tag_bg: yellow 20%
          IM_COL32(138, 173, 244, 51),   // stash_bg: blue 20%
          IM_COL32(54, 58, 79, 255),     // file_header_bg: surface0
          IM_COL32(244, 173, 138, 100),  // search_match_bg: peach
          IM_COL32(244, 173, 138, 200),  // search_current_bg: peach
          IM_COL32(64, 200, 174, 128),   // current_header_bg
          IM_COL32(64, 200, 174, 51),    // current_content_bg
          IM_COL32(64, 166, 255, 128),   // incoming_header_bg
          IM_COL32(64, 166, 255, 51),    // incoming_content_bg
      };
      break;
    case Style::kRedLightDistrict:
      ApplyRedLightDistrictStyle();
      // Vivid neon ANSI palette -- every color cranked up.
      active_ansi_palette = {
          {0xFF0A0410, 0xFF3319F3, 0xFF66FF33, 0xFF26F0F0, 0xFFFF6633,
           0xFFCC33FF, 0xFF19E5E5, 0xFFE0D8DA},
          {0xFF554060, 0xFF6040FF, 0xFF40FF66, 0xFF33FFFF, 0xFFFF3366,
           0xFFFF66CC, 0xFF33FFE5, 0xFFF0E8EA},
      };
      // Neon syntax colors.
      active_syntax_palette = {
          0xFFFF66CC,                   // keyword: neon pink
          0xFF40FF66,                   // string: neon green
          0xFF806090,                   // comment: muted mauve
          0xFF33FFFF,                   // number: neon cyan
          0xFF6040FF,                   // preprocessor: neon purple
          0xFF26F0F0,                   // type: neon yellow
          IM_COL32(0, 120, 0, 100),     // added_bg
          IM_COL32(120, 0, 0, 100),     // removed_bg
          IM_COL32(120, 120, 0, 100),   // tag_bg
          IM_COL32(60, 0, 120, 100),    // stash_bg
          IM_COL32(46, 13, 26, 255),    // file_header_bg: surface0
          IM_COL32(255, 128, 13, 100),  // search_match_bg: neon orange
          IM_COL32(255, 128, 13, 200),  // search_current_bg: neon orange
          IM_COL32(64, 200, 174, 128),  // current_header_bg
          IM_COL32(64, 200, 174, 51),   // current_content_bg
          IM_COL32(64, 166, 255, 128),  // incoming_header_bg
          IM_COL32(64, 166, 255, 51),   // incoming_content_bg
      };
      break;
    case Style::kSystem:
    case Style::kCount:
      NOTREACHED();
      break;
  }
}

float GetSeparatorThickness() {
  switch (active_layout) {
    case Layout::kCompact:
      return 2.0f;
    case Layout::kComfortable:
      return 6.0f;
    case Layout::kRounded:
      return 5.0f;
    case Layout::kFlat:
    case Layout::kDefault:
    case Layout::kCount:
      return 4.0f;
  }
  return 4.0f;
}

uint32_t ResolveColor(ColorId color) {
  switch (color) {
    case ColorId::kDefault:
      return ImGui::GetColorU32(ImGuiCol_Text);
    // Bold with no explicit color is used for diff file headers. Dark themes
    // draw them on a dark background (use bright white); light themes draw
    // them on a light background (use the theme's text color).
    case ColorId::kDefaultBold:
      return IsLightStyle(active_style) ? ImGui::GetColorU32(ImGuiCol_Text)
                                        : active_ansi_palette.bright[7];
    case ColorId::kBlack:
    case ColorId::kRed:
    case ColorId::kGreen:
    case ColorId::kYellow:
    case ColorId::kBlue:
    case ColorId::kMagenta:
    case ColorId::kCyan:
    case ColorId::kWhite:
      return active_ansi_palette
          .normal[static_cast<int>(color) - static_cast<int>(ColorId::kBlack)];
    case ColorId::kBrightBlack:
    case ColorId::kBrightRed:
    case ColorId::kBrightGreen:
    case ColorId::kBrightYellow:
    case ColorId::kBrightBlue:
    case ColorId::kBrightMagenta:
    case ColorId::kBrightCyan:
    case ColorId::kBrightWhite:
      return active_ansi_palette
          .bright[static_cast<int>(color) -
                  static_cast<int>(ColorId::kBrightBlack)];
    case ColorId::kTextDisabled:
      return active_ansi_palette.bright[0];
    case ColorId::kSyntaxKeyword:
      return active_syntax_palette.keyword;
    case ColorId::kSyntaxString:
      return active_syntax_palette.string;
    case ColorId::kSyntaxComment:
      return active_syntax_palette.comment;
    case ColorId::kSyntaxNumber:
      return active_syntax_palette.number;
    case ColorId::kSyntaxPreprocessor:
      return active_syntax_palette.preprocessor;
    case ColorId::kSyntaxType:
      return active_syntax_palette.type;
  }
  NOTREACHED();
  return 0;
}

const SyntaxPalette& GetSyntaxPalette() {
  return active_syntax_palette;
}
