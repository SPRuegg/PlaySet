#pragma once

#include "stem_fingerprint.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace playset {

inline constexpr std::uint32_t stem_collection_fingerprint_version = 1;
inline constexpr std::size_t maximum_fingerprint_stems = 256;

struct StemCollectionFingerprint {
  std::uint32_t version = stem_collection_fingerprint_version;
  std::uint32_t stem_count = 0;
  Sha256Digest digest{};
  [[nodiscard]] std::string hex_digest() const;
  bool operator==(const StemCollectionFingerprint &) const = default;
};

struct FingerprintedStem {
  std::filesystem::path path;
  StemFingerprint fingerprint;
};

struct FingerprintedStems {
  StemCollectionFingerprint fingerprint;
  std::vector<FingerprintedStem> stems;
};

// Reference names are excluded. Results retain the included input order.
// Both functions perform blocking file access outside the audio callback.
[[nodiscard]] FingerprintedStems fingerprint_stems(std::span<const std::filesystem::path> paths);
// False means a completed calculation differs; invalid input or file failures throw.
[[nodiscard]] bool verify_stems(std::span<const std::filesystem::path> paths,
                                const StemCollectionFingerprint &expected);

} // namespace playset
