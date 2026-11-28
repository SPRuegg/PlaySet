#include "songs/song_configuration.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace playset;
using Json = nlohmann::json;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

SongConfiguration configuration() {
  SongConfiguration song;
  song.id = "eb277073-0546-48df-9040-cf4af075df63";
  song.stem_folder = "/Music/Example Song/MultiTracks";
  song.fingerprint.stem_count = 2;
  song.fingerprint.digest.fill(0xef);
  StemFingerprint bass, piano;
  bass.digest.fill(0xab);
  piano.digest.fill(0xcd);
  song.tracks = {{"track-0", "Bass.wav", bass, 0.123456789012345, true, true},
                 {"track-7", "Piano.wav", piano, 1, false, true},
                 {"track-9", "Guide.m4a", std::nullopt, 0, true, false}};
  song.master = {0.375, true};
  return song;
}

template <class Change> void rejects(Change change) {
  auto song = configuration();
  change(song);
  try {
    (void)serialize_song_configuration(song);
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("Invalid configuration was serialized");
}

void serialization() {
  auto song = configuration();
  song.stem_folder = "/Music/Example \"Song\"/Ω\nMultitracks";
  song.tracks[1].file = "Piano \"Live\" Ω\n.wav";
  const auto text = serialize_song_configuration(song);
  require(text == serialize_song_configuration(song) && text.back() == '\n',
          "Serialization is not deterministic or lacks its final newline");
  const auto json = Json::parse(text);
  require(json.size() == 6 && json.at("schemaVersion") == 1 && json.at("id") == song.id &&
              json.at("stemFolder") == song.stem_folder.generic_string(),
          "Song fields or escaped path text were lost");
  const auto &collection = json.at("fingerprint");
  require(collection.size() == 3 && collection.at("version") == 1 &&
              collection.at("stemCount") == 2 && collection.at("digest") == song.fingerprint.hex_digest(),
          "Collection identity was not preserved");
  const auto &tracks = json.at("tracks");
  require(tracks.size() == 3 && tracks[0].size() == 6 && tracks[0].at("id") == "track-0" &&
              tracks[0].at("file") == "Bass.wav" &&
              tracks[0].at("fingerprint").at("version") == stem_fingerprint_version &&
              tracks[0].at("fingerprint").at("digest") == song.tracks[0].fingerprint->hex_digest(),
          "Track identity was not preserved");
  require(tracks[0].at("volume").get<double>() == song.tracks[0].volume &&
              tracks[0].at("mute") == true && tracks[0].at("solo") == true &&
              tracks[1].at("volume") == 1 && tracks[1].at("mute") == false &&
              tracks[1].at("solo") == true && tracks[1].at("file") == song.tracks[1].file.generic_string(),
          "Requested volumes, precision, mute, or additive solos were lost");
  require(tracks[2].at("fingerprint").is_null() && tracks[2].at("volume") == 0 &&
              tracks[2].at("mute") == true && tracks[2].at("solo") == false,
          "Reference settings or silence were lost");
  require(json.at("master").size() == 2 && json.at("master").at("volume") == 0.375 &&
              json.at("master").at("mute") == true,
          "Master settings were lost");

  song.master = {};
  song.tracks[0].volume = 0;
  song.tracks[0].muted = false;
  const auto defaults = Json::parse(serialize_song_configuration(song));
  require(defaults.at("master").at("volume") == 1 && defaults.at("master").at("mute") == false &&
              defaults.at("tracks")[0].at("volume") == 0 &&
              defaults.at("tracks")[0].at("mute") == false,
          "Silence was treated as mute or master defaults changed");
}

void validation() {
  for (const auto version : {0U, 2U}) {
    rejects([&](auto &song) { song.schema_version = version; });
    rejects([&](auto &song) { song.fingerprint.version = version; });
    rejects([&](auto &song) { song.tracks[0].fingerprint->version = version; });
  }
  for (const auto &id : {std::string{}, std::string(129, 'a'), std::string("track 1"),
                         std::string("track/1"), std::string("Ω")}) {
    rejects([&](auto &song) { song.id = id; });
    rejects([&](auto &song) { song.tracks[0].id = id; });
  }
  auto longest_id = configuration();
  longest_id.id = std::string(128, 'a');
  (void)serialize_song_configuration(longest_id);
  for (const auto value : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    rejects([&](auto &song) { song.master.volume = value; });
    rejects([&](auto &song) { song.tracks[0].volume = value; });
  }
  for (const auto *folder : {"", "relative/path"})
    rejects([&](auto &song) { song.stem_folder = folder; });
  rejects([](auto &song) { song.stem_folder = std::string("/Music/\0hidden", 14); });
  for (const auto *file : {"", ".", "..", "../Bass.wav", "nested/Bass.wav", "/Bass.wav", "Bass.mp3"})
    rejects([&](auto &song) { song.tracks[0].file = file; });
  rejects([](auto &song) { song.tracks[0].file = std::string("Bass\0hidden.wav", 15); });
  rejects([](auto &song) { song.stem_folder = std::string("/Music/\xff", 8); });
  rejects([](auto &song) { song.tracks[0].file = std::string("\xff.wav", 5); });
  rejects([](auto &song) { song.tracks[1].id = song.tracks[0].id; });
  rejects([](auto &song) { song.tracks[1].file = song.tracks[0].file; });
  rejects([](auto &song) { song.tracks.clear(); });
  for (const auto count : {0U, 1U, 3U, 257U})
    rejects([&](auto &song) { song.fingerprint.stem_count = count; });
  rejects([](auto &song) {
    for (auto &track : song.tracks) track.fingerprint.reset();
    song.fingerprint.stem_count = 0;
  });

  auto full = configuration();
  full.tracks.clear();
  for (std::size_t i = 0; i < maximum_fingerprint_stems; ++i)
    full.tracks.push_back({"track-" + std::to_string(i), "Stem " + std::to_string(i) + ".WAV",
                           StemFingerprint{}, 1, false, false});
  full.fingerprint.stem_count = static_cast<std::uint32_t>(full.tracks.size());
  (void)serialize_song_configuration(full);
  full.tracks.push_back({"extra", "Extra.wav", StemFingerprint{}, 1, false, false});
  rejects([&](auto &song) { song = full; });
}

void identity() {
  auto song = configuration();
  song.tracks[1].fingerprint = song.tracks[0].fingerprint;
  const auto original = Json::parse(serialize_song_configuration(song));
  std::swap(song.tracks[0], song.tracks[1]);
  song.tracks[0].file = "Renamed Piano.wav";
  song.tracks.erase(song.tracks.begin() + 2);
  const auto changed = Json::parse(serialize_song_configuration(song));
  require(changed.at("id") == original.at("id") &&
              changed.at("tracks")[0].at("id") == "track-7" &&
              changed.at("tracks")[1].at("id") == "track-0" &&
              changed.at("tracks").size() == 2 &&
              changed.at("tracks")[0].at("fingerprint") == changed.at("tracks")[1].at("fingerprint"),
          "Renaming, reordering, or duplicate audio changed stored track IDs");
}
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) throw std::runtime_error("Supply a song configuration test case");
    const std::string_view test(argv[1]);
    if (test == "serialization") serialization();
    else if (test == "validation") validation();
    else if (test == "identity") identity();
    else throw std::runtime_error("Unknown song configuration test case");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
