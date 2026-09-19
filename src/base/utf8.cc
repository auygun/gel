// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/utf8.h"

#include <algorithm>

namespace base {

size_t Utf8SequenceLength(std::string_view s, size_t i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n;
  if (c < 0x80) {
    return 1;
  } else if ((c & 0xE0) == 0xC0) {
    n = 2;
  } else if ((c & 0xF0) == 0xE0) {
    n = 3;
  } else if ((c & 0xF8) == 0xF0) {
    n = 4;
  } else {
    return 1;  // Dangling continuation or otherwise invalid byte.
  }
  return std::min(n, s.size() - i);
}

size_t Utf8SafePrefixLength(std::string_view s, size_t max_bytes) {
  const size_t len = std::min(max_bytes, s.size());
  // Back up so we don't land in the middle of a multi-byte sequence. Avoid
  // indexing s[s.size()], which is out of bounds, by requiring i < s.size().
  size_t i = len;
  while (i > 0 && i < s.size() &&
         (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
    --i;
  return i;
}

}  // namespace base
