// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_STYLE_H
#define GEL_UI_STYLE_H

#include <cstdint>

enum class Style {
  kSystem,
  kDark,
  kLight,
  kCatppuccinMocha,
  kCatppuccinLatte,
  kCatppuccinFrappe,
  kCatppuccinMacchiato,
  kRedLightDistrict,
  kCount
};

enum class Layout { kDefault, kCompact, kComfortable, kRounded, kFlat, kCount };

struct AnsiPalette {
  uint32_t normal[8];  // ABGR byte order.
  uint32_t bright[8];  // ABGR byte order.
};

struct SyntaxPalette {
  uint32_t keyword;
  uint32_t string;
  uint32_t comment;
  uint32_t number;
  uint32_t preprocessor;
  uint32_t type;
  uint32_t added_bg;             // Background for '+' lines.
  uint32_t removed_bg;           // Background for '-' lines.
  uint32_t tag_bg;               // Background for tag labels.
  uint32_t stash_bg;             // Background for stash labels.
  uint32_t file_header_bg;       // Background for diff file header lines.
  uint32_t search_match_bg;      // Background for search matches.
  uint32_t search_current_bg;    // Background for the current search match.
  uint32_t current_header_bg;    // Conflict: <<<<<<< marker line.
  uint32_t current_content_bg;   // Conflict: lines between <<<<<<< and =======.
  uint32_t incoming_header_bg;   // Conflict: >>>>>>> marker line.
  uint32_t incoming_content_bg;  // Conflict: lines between ======= and >>>>>>>.
};

// Color IDs stored in ColoredLine segments and resolved to real ABGR colors at
// render time via ResolveColor().
enum class ColorId {
  kDefault,
  kDefaultBold,
  kBlack,
  kRed,
  kGreen,
  kYellow,
  kBlue,
  kMagenta,
  kCyan,
  kWhite,
  kBrightBlack,
  kBrightRed,
  kBrightGreen,
  kBrightYellow,
  kBrightBlue,
  kBrightMagenta,
  kBrightCyan,
  kBrightWhite,
  kTextDisabled,
  kSyntaxKeyword,
  kSyntaxString,
  kSyntaxComment,
  kSyntaxNumber,
  kSyntaxPreprocessor,
  kSyntaxType,
};

const char* GetStyleName(Style style);
const char* GetLayoutName(Layout layout);
// Returns true if |style| is a light theme. kSystem is not light; callers
// must resolve it against the OS dark mode setting first.
bool IsLightStyle(Style style);
// |os_dark_mode| is used when style is kSystem to pick Dark or Light.
void ApplyStyle(Style style, Layout layout, bool os_dark_mode = true);

// Returns the base separator thickness for the active layout (before DPI
// scaling).
float GetSeparatorThickness();

// Resolves a ColorId to the active style's ABGR color.
uint32_t ResolveColor(ColorId color);

// Returns the active syntax highlighting palette.
const SyntaxPalette& GetSyntaxPalette();

#endif  // GEL_UI_STYLE_H
