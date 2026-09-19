// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#ifndef BASE_UTF8_H
#define BASE_UTF8_H

#include <cstddef>
#include <string_view>

namespace base {

// Returns the byte length of the UTF-8 sequence starting at s[i], clamped to
// the remaining buffer. Returns 1 for ASCII bytes and for dangling/invalid
// continuation bytes. Callers must pass i < s.size().
size_t Utf8SequenceLength(std::string_view s, size_t i);

// Returns the largest byte index <= max_bytes that lies on a valid UTF-8
// sequence boundary (i.e. not in the middle of a multi-byte character), so a
// prefix truncated to the returned length is always valid UTF-8.
size_t Utf8SafePrefixLength(std::string_view s, size_t max_bytes);

}  // namespace base

#endif  // BASE_UTF8_H
