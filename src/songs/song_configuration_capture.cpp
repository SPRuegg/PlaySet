#include "song_configuration_capture.hpp"

#include "macos/song_id.hpp"
#include "playback/transport/transport.hpp"

#include <map>
#include <stdexcept>

namespace playset {

SongConfigurationCapture::SongConfigurationCapture(
    Transport &transport, const std::filesystem::path &stem_folder)
    : transport_(transport), id_(detail::create_song_id()),
      stem_folder_(std::filesystem::canonical(stem_folder)) {
  if (!std::filesystem::is_directory(stem_folder_))
    throw std::invalid_argument("The song source must be a stem folder.");
  const auto stems = transport_.stems();
  sources_.reserve(stems.size());
  for (std::size_t i = 0; i < stems.size(); ++i) {
    if (stems[i].path.parent_path() != stem_folder_)
      throw std::invalid_argument("The stem folder must contain the transport's loaded sources directly.");
    sources_.push_back({i, "track-" + std::to_string(i), stems[i].path});
  }
}

SongConfiguration SongConfigurationCapture::capture() {
  const auto settings = transport_.tracks();
  const auto state = transport_.snapshot();
  if (settings.size() != sources_.size())
    throw std::runtime_error("Loaded tracks changed; create a new configuration capture.");

  SongConfiguration song;
  song.id = id_;
  song.stem_folder = stem_folder_;
  song.master = {state.master_volume, state.master_muted};
  std::vector<std::filesystem::path> paths;
  paths.reserve(sources_.size());
  song.tracks.reserve(sources_.size());
  for (const auto &source : sources_) {
    const auto &track = settings.at(source.track_id);
    if (track.id != source.track_id)
      throw std::runtime_error("Loaded track identities changed; create a new configuration capture.");
    song.tracks.push_back({source.id, source.path.filename(), std::nullopt,
                           track.volume, track.muted, track.solo});
    paths.push_back(source.path);
  }

  const auto fingerprints = fingerprint_stems(paths);
  if (expected_ && fingerprints.fingerprint != *expected_)
    throw std::runtime_error("Source stems no longer match this song configuration.");
  std::map<std::filesystem::path, StemFingerprint> by_source;
  for (const auto &stem : fingerprints.stems)
    by_source.emplace(stem.path, stem.fingerprint);
  for (std::size_t i = 0; i < sources_.size(); ++i) {
    const auto found = by_source.find(sources_[i].path);
    if (found != by_source.end()) song.tracks[i].fingerprint = found->second;
  }
  song.fingerprint = fingerprints.fingerprint;
  expected_ = song.fingerprint;
  return song;
}

} // namespace playset
