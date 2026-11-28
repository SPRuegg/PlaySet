#include "songs/song_configuration_capture.hpp"
#include "playback/transport/transport.hpp"
#include "fixture_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace playset;
using namespace playset::test;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  try { action(); } catch (const std::exception &) { return; }
  throw std::runtime_error("Invalid source capture was accepted");
}

struct SilentOutput final : PlaybackOutput {
  BufferedPlayback &playback;
  bool running = false;
  unsigned pauses = 0;
  std::int64_t position = 0;
  explicit SilentOutput(BufferedPlayback &source) : playback(source) {}
  void play() override { running = true; }
  void pause() override { running = false; ++pauses; }
  void seek(std::int64_t frame) override {
    playback.prepare_seek(frame);
    const auto origin = playback.apply_pending_seek();
    require(origin.has_value(), "Seek was not ready");
    position = *origin;
    playback.acknowledge_seek();
  }
  std::int64_t position_frames() const override { return position; }
  bool finished() override { return playback.exhausted(); }
  void advance() {
    std::array<float, 256> output{};
    if (running) position += playback.render(output);
  }
};

struct Loaded {
  TempFolder folder;
  SilentOutput *output = nullptr;
  unsigned output_count = 0;
  std::unique_ptr<Transport> transport;
  Loaded() {
    for (const auto *name : {"Bass Copy.wav", "Bass.wav", "Guide.wav"})
      write_pcm_wav(folder.path / name, 8000, 1, 16, 8000);
    transport = std::make_unique<Transport>(prepare_stems(folder.path),
        [&](BufferedPlayback &playback, double) {
          ++output_count;
          auto device = std::make_unique<SilentOutput>(playback);
          output = device.get();
          return device;
        });
  }
};

void settings() {
  Loaded loaded;
  auto &transport = *loaded.transport;
  const auto stems = transport.stems();
  require(stems.size() == 3 && stems[0].path == fs::canonical(loaded.folder.path / "Bass Copy.wav"),
          "Transport source order differs from track IDs");
  SongConfigurationCapture capture(transport, loaded.folder.path);
  transport.play();
  loaded.output->advance();
  transport.set_track_volume(0, 0.25);
  transport.set_track_muted(0, true);
  transport.set_track_volume(1, 0.5);
  transport.set_track_volume(2, 0);
  const std::array<TrackId, 2> solos{0, 1};
  transport.set_tracks_soli(solos, true);
  transport.set_master_volume(0.375);
  transport.set_master_muted(true);
  const auto before = transport.snapshot();
  const auto song = capture.capture();
  require(song.stem_folder == fs::canonical(loaded.folder.path) && song.tracks.size() == 3 &&
              song.fingerprint.stem_count == 2, "Capture lost its folder or source counts");
  require(song.tracks[0].file == "Bass Copy.wav" && song.tracks[0].volume == 0.25 &&
              song.tracks[0].muted && song.tracks[0].solo &&
              song.tracks[1].volume == 0.5 && !song.tracks[1].muted && song.tracks[1].solo,
          "Capture lost requested volume, mute, or additive solo settings");
  require(song.tracks[2].file == "Guide.wav" && !song.tracks[2].fingerprint &&
              song.tracks[2].volume == 0 && !song.tracks[2].muted && !song.tracks[2].solo,
          "Reference track settings were omitted or silence became mute");
  require(song.tracks[0].fingerprint && song.tracks[0].fingerprint == song.tracks[1].fingerprint &&
              song.tracks[0].id != song.tracks[1].id,
          "Duplicate audio lost its separate track identities");
  require(song.master.volume == 0.375 && song.master.muted, "Master settings were not captured");
  const auto after = transport.snapshot();
  require(after.status == before.status && after.position_seconds == before.position_seconds &&
              loaded.output->running && loaded.output->pauses == 0 && loaded.output_count == 1,
          "Capture changed playback or recreated its output");
  loaded.output->advance();
  require(transport.snapshot().position_seconds > after.position_seconds,
          "Playback stopped advancing after capture");
  (void)serialize_song_configuration(song);
}

void identity() {
  Loaded loaded;
  auto &transport = *loaded.transport;
  SongConfigurationCapture capture(transport, loaded.folder.path);
  const auto first = capture.capture();
  require(first.id.size() == 36 && first.id[14] == '4', "Song ID is not a random UUID");
  transport.set_track_volume(0, 0.125);
  transport.set_track_muted(2, true);
  transport.set_master_volume(0.25);
  transport.seek(0.25);
  transport.play();
  transport.pause();
  const auto paused = capture.capture();
  transport.stop();
  const auto stopped = capture.capture();
  require(first.id == paused.id && first.id == stopped.id && first.fingerprint == stopped.fingerprint,
          "Repeated capture changed song identity");
  for (std::size_t i = 0; i < first.tracks.size(); ++i)
    require(first.tracks[i].id == paused.tracks[i].id && first.tracks[i].id == stopped.tracks[i].id &&
                first.tracks[i].file == stopped.tracks[i].file,
            "Transport operations changed persistent track IDs or source association");
  require(first.tracks[0].volume == 1 && !first.tracks[2].muted && first.master.volume == 1 &&
              stopped.tracks[0].volume == 0.125 && stopped.tracks[2].muted && stopped.master.volume == 0.25,
          "Capture changed an earlier returned configuration or missed later edits");
  SongConfigurationCapture separate(transport, loaded.folder.path);
  require(separate.capture().id != first.id, "A separate configuration reused the song ID");
  TempFolder links;
  fs::create_directory_symlink(loaded.folder.path, links.path / "source");
  SongConfigurationCapture linked(transport, links.path / "source");
  require(linked.capture().stem_folder == first.stem_folder,
          "A folder alias did not retain the canonical source location");

  write_pcm_wav(loaded.folder.path / "Guide replacement.wav", 8000, 1, 16, 4000);
  fs::rename(loaded.folder.path / "Guide replacement.wav", loaded.folder.path / "Guide.wav");
  write_pcm_wav(loaded.folder.path / "New track.wav", 8000, 1, 16, 8000);
  const auto references = capture.capture();
  require(references.fingerprint == first.fingerprint && references.tracks.size() == first.tracks.size(),
          "Changed reference audio or an unloaded file changed the loaded song's identity");
}

void failures() {
  Loaded loaded;
  auto &transport = *loaded.transport;
  TempFolder other;
  fs::copy_file(loaded.folder.path / "Bass.wav", other.path / "Bass.wav");
  rejects([&] { SongConfigurationCapture wrong(transport, other.path); });
  rejects([&] { SongConfigurationCapture missing(transport, loaded.folder.path / "missing"); });
  rejects([&] { SongConfigurationCapture file(transport, loaded.folder.path / "Bass.wav"); });

  SongConfigurationCapture capture(transport, loaded.folder.path);
  const auto source = loaded.folder.path / "Bass.wav";
  const auto held = loaded.folder.path / "held";
  fs::rename(source, held);
  rejects([&] { (void)capture.capture(); });
  fs::rename(held, source);
  const auto original = capture.capture();

  const auto replacement = loaded.folder.path / "replacement";
  write_pcm_wav(replacement, 8000, 1, 16, 8000);
  { std::ofstream edited(replacement, std::ios::binary | std::ios::in);
    edited.seekp(44); write_little_endian(edited, 4096, 2); }
  fs::rename(source, held);
  fs::rename(replacement, source);
  auto preserved = original;
  rejects([&] { preserved = capture.capture(); });
  require(preserved.id == original.id && preserved.fingerprint == original.fingerprint,
          "Failed capture published a partial configuration");
  fs::rename(held, source);
  require(capture.capture().id == original.id, "Failed verification changed the capture identity");
  require(transport.snapshot().status == TransportStatus::stopped && loaded.output_count == 1,
          "Failed capture changed the current transport");
}
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) throw std::runtime_error("Supply a song capture test case");
    const std::string_view test(argv[1]);
    if (test == "settings") settings();
    else if (test == "identity") identity();
    else if (test == "failures") failures();
    else throw std::runtime_error("Unknown song capture test case");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
