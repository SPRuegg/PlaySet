#include "song_configuration.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace playset {
namespace {
using Json = nlohmann::ordered_json;

void validate_id(std::string_view id) {
  if (id.empty() || id.size() > 128 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_';
      }))
    throw std::invalid_argument("Song and track IDs require 1–128 ASCII letters, digits, hyphens, or underscores.");
}

void validate_volume(double volume) {
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::invalid_argument("Song volumes must be finite values from 0 through 1.");
}

bool contains_null(const std::filesystem::path &path) {
  return path.native().find('\0') != std::filesystem::path::string_type::npos;
}

void validate(const SongConfiguration &song) {
  if (song.schema_version != song_schema_version)
    throw std::invalid_argument("Unsupported song configuration schema version.");
  validate_id(song.id);
  if (!song.stem_folder.is_absolute() || contains_null(song.stem_folder))
    throw std::invalid_argument("The stem folder must be an absolute path without null characters.");
  if (song.tracks.empty() || song.tracks.size() > maximum_fingerprint_stems)
    throw std::invalid_argument("A song configuration requires between 1 and 256 tracks.");
  if (song.fingerprint.version != stem_collection_fingerprint_version)
    throw std::invalid_argument("Unsupported collection fingerprint version.");
  validate_volume(song.master.volume);

  std::set<std::string> ids;
  std::set<std::filesystem::path> files;
  std::uint32_t included_count = 0;
  for (const auto &track : song.tracks) {
    validate_id(track.id);
    if (!ids.insert(track.id).second)
      throw std::invalid_argument("Track IDs must be unique within a song configuration.");
    auto extension = track.file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    if (track.file.empty() || track.file.has_parent_path() || contains_null(track.file) ||
        (extension != ".wav" && extension != ".m4a"))
      throw std::invalid_argument("Each track file must be a WAV or M4A filename inside the stem folder.");
    if (!files.insert(track.file).second)
      throw std::invalid_argument("Track filenames must be unique within a song configuration.");
    validate_volume(track.volume);
    if (track.fingerprint) {
      if (track.fingerprint->version != stem_fingerprint_version)
        throw std::invalid_argument("Unsupported individual fingerprint version.");
      ++included_count;
    }
  }
  if (included_count == 0 || included_count != song.fingerprint.stem_count)
    throw std::invalid_argument("Collection stem count must match tracks with individual fingerprints.");
}

Json encode(const StemFingerprint &fingerprint) {
  return {{"version", fingerprint.version}, {"digest", fingerprint.hex_digest()}};
}
}

std::string serialize_song_configuration(const SongConfiguration &song) {
  validate(song);
  Json tracks = Json::array();
  for (const auto &track : song.tracks) {
    tracks.push_back({{"id", track.id}, {"file", track.file.generic_string()},
                      {"fingerprint", track.fingerprint ? encode(*track.fingerprint) : Json(nullptr)},
                      {"volume", track.volume}, {"mute", track.muted}, {"solo", track.solo}});
  }
  const Json document{
      {"schemaVersion", song.schema_version}, {"id", song.id},
      {"stemFolder", song.stem_folder.generic_string()},
      {"fingerprint", {{"version", song.fingerprint.version},
                       {"stemCount", song.fingerprint.stem_count},
                       {"digest", song.fingerprint.hex_digest()}}},
      {"tracks", std::move(tracks)},
      {"master", {{"volume", song.master.volume}, {"mute", song.master.muted}}}};
  try {
    return document.dump(2) + '\n';
  } catch (const Json::type_error &) {
    throw std::invalid_argument("Song configuration text must use valid UTF-8.");
  }
}

} // namespace playset
