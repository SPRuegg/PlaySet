#pragma once

#include "audio/fingerprinting/stem_collection.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace playset {

inline constexpr std::uint32_t song_schema_version = 1;

struct SongTrackConfiguration {
  std::string id;
  std::filesystem::path file;
  std::optional<StemFingerprint> fingerprint;
  double volume = 1;
  bool muted = false;
  bool solo = false;
};

struct SongMasterConfiguration {
  double volume = 1;
  bool muted = false;
};

struct SongConfiguration {
  std::uint32_t schema_version = song_schema_version;
  std::string id;
  std::filesystem::path stem_folder;
  StemCollectionFingerprint fingerprint;
  std::vector<SongTrackConfiguration> tracks;
  SongMasterConfiguration master;
};

// Validates and returns JSON without accessing source audio or writing a file.
[[nodiscard]] std::string serialize_song_configuration(const SongConfiguration &song);

} // namespace playset
