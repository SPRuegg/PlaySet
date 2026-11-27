#pragma once

#include "macos/sha256.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

namespace playset::detail {

template <class T> void hash_integer(Sha256 &hash, T value) {
  static_assert(std::is_unsigned_v<T>);
  std::array<std::uint8_t, sizeof(T)> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<std::uint8_t>(value >> (i * 8));
  hash.update(bytes);
}

inline std::string hex_digest(const Sha256Digest &digest) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result(digest.size() * 2, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    result[i * 2] = digits[digest[i] >> 4];
    result[i * 2 + 1] = digits[digest[i] & 15];
  }
  return result;
}

} // namespace playset::detail
