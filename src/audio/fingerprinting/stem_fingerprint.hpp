#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace playset {

inline constexpr std::uint32_t stem_fingerprint_version = 1;
inline constexpr unsigned stem_sample_seconds = 1;
inline constexpr std::array<unsigned, 3> stem_sample_percentages{10, 50, 90};
using Sha256Digest = std::array<std::uint8_t, 32>;

struct StemFingerprint {
  std::uint32_t version = stem_fingerprint_version;
  Sha256Digest digest{};
  [[nodiscard]] std::string hex_digest() const;
  bool operator==(const StemFingerprint &) const = default;
};

// Blocking file access with its own reader; never call from an audio callback.
[[nodiscard]] StemFingerprint fingerprint_stem(const std::filesystem::path &path);

} // namespace playset
