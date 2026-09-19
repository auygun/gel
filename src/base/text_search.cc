// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/text_search.h"

#include <algorithm>

namespace base {

void ToLowerAscii(std::string& out, std::string_view src) {
  out.resize(src.size());
  std::transform(src.begin(), src.end(), out.begin(),
                 [](char c) { return ToLowerAscii(c); });
}

SearchTerm::SearchTerm(std::string_view term, MatchOptions options)
    : options_(options) {
  if (options.case_sensitive)
    needle_.assign(term);
  else
    ToLowerAscii(needle_, term);
}

bool Contains(std::string_view text, const SearchTerm& term) {
  if (term.empty() || term.size() > text.size())
    return false;

  const std::string& needle = term.needle();
  bool whole_word = term.options().whole_word;

  // Both branches keep searching past a candidate that whole word rejects, and
  // resume one byte along rather than past it, since a match can start inside
  // the rejected span. Without whole word each returns on the first candidate,
  // exactly as they did before.
  if (term.options().case_sensitive) {
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
      if (!whole_word || IsWordBounded(text, pos, needle.size()))
        return true;
      pos++;
    }
    return false;
  }

  // The needle is already folded, so only the haystack byte needs folding.
  auto folded_eq = [](char a, char b) { return ToLowerAscii(a) == b; };
  auto begin = text.begin();
  for (;;) {
    auto it =
        std::search(begin, text.end(), needle.begin(), needle.end(), folded_eq);
    if (it == text.end())
      return false;
    size_t pos = static_cast<size_t>(it - text.begin());
    if (!whole_word || IsWordBounded(text, pos, needle.size()))
      return true;
    begin = it + 1;
  }
}

}  // namespace base
