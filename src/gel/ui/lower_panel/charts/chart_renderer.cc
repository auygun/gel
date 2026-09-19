// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "gel/ui/lower_panel/charts/chart_renderer.h"

#include <algorithm>
#include <cstdio>

#include "third_party/imgui/imgui/imgui.h"

namespace {
constexpr const char* kSizePrefixes[] = {"B", "kB", "MB", "GB"};
}  // namespace

std::string HumanSize(int64_t bytes) {
  int idx = 0;
  double value = static_cast<double>(bytes);
  while (value >= 1024.0 && idx < 3) {
    value /= 1024.0;
    ++idx;
  }
  if (idx == 0)
    return std::to_string(bytes) + " B";
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f %s", value, kSizePrefixes[idx]);
  return buf;
}

ImU32 SliceColor(int index) {
  // Rich, vibrant palette with good contrast.
  static const ImU32 kColors[] = {
      IM_COL32(0x64, 0x9E, 0xD6, 0xFF),  // sky blue
      IM_COL32(0xF0, 0x82, 0x4E, 0xFF),  // warm orange
      IM_COL32(0x5B, 0xC4, 0x8A, 0xFF),  // emerald green
      IM_COL32(0xE8, 0x6B, 0x78, 0xFF),  // coral red
      IM_COL32(0xB0, 0x8C, 0xD6, 0xFF),  // soft purple
      IM_COL32(0xF2, 0xC8, 0x55, 0xFF),  // golden yellow
      IM_COL32(0x4E, 0xC5, 0xC1, 0xFF),  // turquoise
      IM_COL32(0xE8, 0x7C, 0xAC, 0xFF),  // rose pink
      IM_COL32(0x8B, 0xB4, 0x5E, 0xFF),  // olive green
      IM_COL32(0xC4, 0x96, 0x6E, 0xFF),  // warm tan
  };
  return kColors[index % 10];
}

ImU32 SliceColorDark(int index) {
  ImU32 col = SliceColor(index);
  int r = ((col >> 0) & 0xFF) * 65 / 100;
  int g = ((col >> 8) & 0xFF) * 65 / 100;
  int b = ((col >> 16) & 0xFF) * 65 / 100;
  return IM_COL32(r, g, b, 255);
}

ImU32 SliceColorLight(int index) {
  ImU32 col = SliceColor(index);
  int r = std::min(255, static_cast<int>((col >> 0) & 0xFF) + 35);
  int g = std::min(255, static_cast<int>((col >> 8) & 0xFF) + 35);
  int b = std::min(255, static_cast<int>((col >> 16) & 0xFF) + 35);
  return IM_COL32(r, g, b, 255);
}

ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
  int ra = (a >> 0) & 0xFF, ga = (a >> 8) & 0xFF, ba = (a >> 16) & 0xFF;
  int rb = (b >> 0) & 0xFF, gb = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
  int r = ra + static_cast<int>((rb - ra) * t);
  int g = ga + static_cast<int>((gb - ga) * t);
  int bl = ba + static_cast<int>((bb - ba) * t);
  return IM_COL32(r, g, bl, 255);
}
