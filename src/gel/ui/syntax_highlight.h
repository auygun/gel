// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_SYNTAX_HIGHLIGHT_H
#define GEL_UI_SYNTAX_HIGHLIGHT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gel/ui/colored_line.h"

enum class Language {
  kNone,
  kCLike,
  kPython,
  kShell,
  kRuby,
  kHtml,
  kJson,
  kGn,
  kShader,
};

// Returns a human-readable name for a language (e.g. "C-like", "Python").
const char* GetLanguageName(Language lang);

// Parses a language name back to the enum. Returns kNone if unknown.
Language ParseLanguageName(const std::string& name);

// Returns the number of languages (excluding kNone).
constexpr int kLanguageCount = static_cast<int>(Language::kShader);

// Detects the programming language from a file path's extension.
// Custom mappings (ext -> language name pairs) are checked first.
Language DetectLanguage(const std::string& filename,
                        const std::vector<std::pair<std::string, std::string>>&
                            custom_mappings = {});

// Multi-line construct (block comment or string) that a line starts inside.
struct SyntaxState {
  enum class Kind : uint8_t {
    kNone,
    kBlockComment,       // /* */ or <!-- -->
    kRawString,          // C++ R"delim( )delim"
    kTripleDoubleQuote,  // Python """ """
    kTripleSingleQuote,  // Python ''' '''
  };
  // Longest raw string delimiter allowed by the C++ standard.
  static constexpr size_t kMaxDelimiterSize = 16;

  Kind kind = Kind::kNone;
  // Raw string delimiter. Unused bytes are zero so that states compare equal.
  uint8_t delimiter_size = 0;
  char delimiter[kMaxDelimiterSize] = {};

  std::string_view GetDelimiter() const { return {delimiter, delimiter_size}; }
  bool operator==(const SyntaxState&) const = default;
};

// Returns the state at the end of |code|, a line without its diff prefix, that
// starts in |state|.
SyntaxState ScanSyntaxState(std::string_view code,
                            Language lang,
                            SyntaxState state);

// Applies syntax highlighting to a diff line. Only processes lines with
// ' ', '+', or '-' prefix. Returns an empty ColoredLine for headers and
// hunk markers. |state| is the multi-line construct the line starts inside, as
// returned by ScanSyntaxState() for the previous line. |prefix_width| is the
// number of diff prefix characters: 1 for a normal diff, one per parent for a
// combined diff.
ColoredLine HighlightSyntax(const std::string& line,
                            Language lang,
                            SyntaxState state = {},
                            size_t prefix_width = 1);

#endif  // GEL_UI_SYNTAX_HIGHLIGHT_H
