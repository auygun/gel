// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_COLORED_LINE_H
#define GEL_UI_COLORED_LINE_H

#include <string>
#include <vector>

#include "gel/ui/style.h"

// A line of text with per-segment color information.
struct ColoredLine {
  struct Segment {
    std::string text;
    ColorId color;
  };
  std::vector<Segment> segments;
};

#endif  // GEL_UI_COLORED_LINE_H
