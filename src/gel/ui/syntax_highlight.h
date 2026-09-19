// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef GEL_UI_SYNTAX_HIGHLIGHT_H
#define GEL_UI_SYNTAX_HIGHLIGHT_H

#include <string>
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

// Applies syntax highlighting to a diff line. Only processes lines with
// ' ', '+', or '-' prefix. Returns an empty ColoredLine for headers and
// hunk markers. When in_block_comment is true, the line starts inside a
// multi-line block comment (e.g. /* */ or <!-- -->).
ColoredLine HighlightSyntax(const std::string& line,
                            Language lang,
                            bool in_block_comment = false);

#endif  // GEL_UI_SYNTAX_HIGHLIGHT_H
