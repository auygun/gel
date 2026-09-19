// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_TEXT_SEARCH_H
#define BASE_TEXT_SEARCH_H

#include <string>
#include <string_view>

namespace base {

// Plain substring search with an optional case-insensitive mode, shared by
// everything in the app that answers "does this text contain what the user
// typed?". Case folding is ASCII-only and deliberately in one place: several
// callers search the same term over the same text and then have to agree with
// each other, which they cannot do if each rolls its own comparison.

// Lowercases an ASCII letter, leaving every other byte alone.
inline char ToLowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

// Writes |src| into |out| with ASCII letters lowercased. |out| is reused, so a
// caller scanning many strings in a row allocates at most once.
void ToLowerAscii(std::string& out, std::string_view src);

// How a search matches. Carried as one value so that the flags travel with
// each other and with the term, rather than as loose bools a call site can
// pair up wrongly.
struct MatchOptions {
  bool case_sensitive = false;
  bool whole_word = false;

  bool operator==(const MatchOptions& other) const = default;
};

// True for bytes that count as part of a word: ASCII letters, digits and '_',
// plus every non-ASCII byte. The cast matters: char is signed here, so a
// plain comparison would read every byte of a multi-byte character as a
// non-word byte and find word boundaries inside "café".
inline bool IsWordByte(char c) {
  auto b = static_cast<unsigned char>(c);
  return b >= 0x80 || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
         (b >= '0' && b <= '9') || b == '_';
}

// True when the |len| bytes at |pos| are not cut out of a longer word. A side
// only has to be clear when the term's own byte on that side is a word byte,
// which is what \b means: it keeps a term that begins or ends in punctuation
// ("->") matching, where requiring a clear side unconditionally would let it
// match nothing.
inline bool IsWordBounded(std::string_view text, size_t pos, size_t len) {
  if (pos > 0 && IsWordByte(text[pos]) && IsWordByte(text[pos - 1]))
    return false;
  size_t end = pos + len;
  return !(end < text.size() && IsWordByte(text[end - 1]) &&
           IsWordByte(text[end]));
}

// A term prepared for matching: holds the needle already lowercased when the
// search ignores case, so scanning many strings folds the term once. Carrying
// the options with the term also keeps the two from being passed around apart.
class SearchTerm {
 public:
  SearchTerm() = default;
  SearchTerm(std::string_view term, MatchOptions options);

  bool empty() const { return needle_.empty(); }
  size_t size() const { return needle_.size(); }
  const MatchOptions& options() const { return options_; }

  // The needle as matched against: lowercased unless the search is case
  // sensitive. Not the text the user typed.
  const std::string& needle() const { return needle_; }

  bool operator==(const SearchTerm& other) const = default;

 private:
  std::string needle_;
  MatchOptions options_;
};

// True when |text| contains |term|. Allocation-free, which makes it the one to
// use for a single question about a short string.
bool Contains(std::string_view text, const SearchTerm& term);

// Calls |on_match(offset)| for every non-overlapping occurrence of |term| that
// its options accept, in order, with |offset| a byte index into |text|.
// |scratch| is owned by the caller and reused for the folded text, so scanning
// line after line allocates at most once. An empty term produces no matches.
template <typename Fn>
void ForEachMatch(std::string_view text,
                  const SearchTerm& term,
                  std::string& scratch,
                  Fn on_match) {
  if (term.empty() || term.size() > text.size())
    return;

  std::string_view haystack = text;
  if (!term.options().case_sensitive) {
    ToLowerAscii(scratch, text);
    haystack = scratch;
  }

  // Boundaries are tested on the folded text: ToLowerAscii preserves length
  // and only maps A-Z to a-z, both word bytes, so offsets and word-ness are
  // the same in either buffer. Revisit if folding ever touches other bytes.
  bool whole_word = term.options().whole_word;
  size_t pos = 0;
  while ((pos = haystack.find(term.needle(), pos)) != std::string_view::npos) {
    if (whole_word && !IsWordBounded(haystack, pos, term.size())) {
      // One byte, not the term length: a match can start inside a candidate
      // that was rejected for sitting against a word byte.
      pos++;
      continue;
    }
    on_match(pos);
    pos += term.size();
  }
}

}  // namespace base

#endif  // BASE_TEXT_SEARCH_H
