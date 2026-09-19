#ifndef BASE_HASH_H
#define BASE_HASH_H

#include <cstddef>
#include <string>

namespace base {

template <size_t N>
constexpr inline size_t KR2Hash(const char (&str)[N], size_t Len = N - 1) {
  size_t hash_value = 0;
  for (int i = 0; str[i] != '\0'; ++i)
    hash_value = str[i] + 31 * hash_value;
  return hash_value;
}

inline size_t KR2Hash(const std::string& str) {
  size_t hash_value = 0;
  for (std::string::value_type c : str)
    hash_value = c + 31 * hash_value;
  return hash_value;
}

inline size_t HashBytes(const void* data, size_t size, size_t seed = 0) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    seed ^= bytes[i];
    seed *= static_cast<size_t>(0x100000001b3);
  }
  return seed;
}

}  // namespace base

#endif  // BASE_HASH_H
