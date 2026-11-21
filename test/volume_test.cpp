#include "fixture_support.hpp"
#include "playback/transport/transport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

using namespace playset;
using namespace playset::test;
namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void ramps_and_mixing() {
  StemBlock block(2);
  block.frames = playback_block_frames;
  std::fill(block.track(0).begin(), block.track(0).end(), 0.25F);
  std::fill(block.track(1).begin(), block.track(1).end(), 0.5F);
  TrackVolumes volumes(2);
  require(volumes.get(0) == 1 && volumes.get(1) == 1, "Tracks must default to original level");
  volumes.set(0, 0);
  volumes.set(1, 0.5);
  VolumeSmoother ramp(volumes, 48000, VolumeSmoothing::linear);
  std::array<float, playback_block_frames * output_channels> audio{};
  ramp.begin_render();
  require(mix_stem_block(block, 0, audio, &ramp) == 0, "Unexpected clipping");
  require(std::all_of(audio.begin(), audio.end(), [](float value) { return value == 0.25F; }),
          "Pre-play levels must apply to the first sample and both channels");
  volumes.set(0, 1);
  ramp.begin_render();
  mix_stem_block(block, 0, audio, &ramp);
  for (std::size_t frame = 0; frame < 240; ++frame) {
    const auto expected = 0.25 + 0.25 * static_cast<double>(frame + 1) / 240;
    require(std::abs(audio[frame * 2] - expected) < 0.000001, "Volume ramp is not 5ms or not independent");
    require(audio[frame * 2] == audio[frame * 2 + 1], "Stereo gain differs");
  }
  require(audio.back() == 0.5F, "Ramp did not reach its exact target");
  volumes.set(0, 0);
  volumes.set(1, 0);
  ramp.begin_render();
  mix_stem_block(block, 0, std::span(audio).first(80), &ramp);
  // A new target midway through a ramp starts from its current value.
  const auto prior = audio[78];
  volumes.set(0, 0.75);
  ramp.begin_render();
  mix_stem_block(block, 40, std::span(audio).first(80), &ramp);
  require(std::abs(audio[0] - prior) < 0.005, "Interrupted ramp caused an abrupt jump");
  volumes.set(0, 0);
  ramp.begin_render();
  mix_stem_block(block, 0, audio, &ramp);
  require(audio.back() == 0, "Zero volume did not reach exact silence");
  for (double invalid : {-0.01, 1.01, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected = false;
    try { volumes.set(0, invalid); } catch (const std::invalid_argument &) { rejected = true; }
    require(rejected && volumes.get(0) == 0, "Invalid volume changed state");
  }
  bool rejected = false;
  try { volumes.set(2, 0.5); } catch (const std::out_of_range &) { rejected = true; }
  require(rejected, "Unknown track ID was accepted");
}
struct OfflineOutput final : PlaybackOutput {
  BufferedPlayback &playback;
  bool running = false;
  std::uint64_t frame = 0;
  explicit OfflineOutput(BufferedPlayback &source) : playback(source) {}
  void play() override { playback.settle_volumes(); running = true; }
  void pause() override { running = false; }
  std::uint64_t position_frames() const override { return frame; }
  bool finished() override { return playback.exhausted(); }
  std::vector<float> advance(std::size_t count) {
    std::vector<float> output(count * output_channels);
    if (running) {
      const auto frames = playback.render(output);
      require(frames == count, "Volume update lost source frames");
      frame += frames;
    }
    return output;
  }
};
void transport_volumes(VolumeSmoothing mode) {
  TempFolder folder;
  write_pcm_wav(folder.path / "A.wav", 48000, 1, 16, 48000);
  write_pcm_wav(folder.path / "B.wav", 48000, 1, 16, 48000);
  OfflineOutput *device = nullptr;
  std::size_t outputs_created = 0;
  auto factory = [&](BufferedPlayback &playback, double) {
    ++outputs_created;
    auto output = std::make_unique<OfflineOutput>(playback);
    device = output.get();
    return output;
  };
  Transport transport(prepare_stems(folder.path), factory, mode);
  const auto original = transport.tracks();
  require(original.size() == 2 && original[0].name == "A" && original[1].name == "B",
          "Track listing lost source names");
  transport.set_track_volume(original[0].id, 0);
  transport.set_track_volume(original[1].id, 0.5);
  transport.play();
  require(device->advance(512).front() == 0.125F, "First audio used stale initial volumes");
  transport.set_track_volume(original[0].id, 1);
  const auto audio = device->advance(512);
  const auto expected = mode == VolumeSmoothing::linear ? 0.375 :
      0.125 + 0.25 * (1 - std::exp(-512.0 / (48000 * 0.005)));
  require(audio.front() > 0.125F && std::abs(audio.back() - expected) < 0.000001,
          "Live update did not apply the selected smoothing to buffered stems");
  transport.pause();
  const auto paused = transport.snapshot().position_seconds;
  fs::rename(folder.path / "A.wav", folder.path / "moved-A.wav");
  fs::rename(folder.path / "B.wav", folder.path / "moved-B.wav");
  transport.set_track_volume(original[0].id, 0);
  transport.set_track_volume(original[1].id, 0);
  require(transport.snapshot().position_seconds == paused, "Volume edit changed paused position");
  transport.play();
  require(device->advance(512).front() == 0, "Paused edit leaked audio on resume");
  require(outputs_created == 1, "Volume changes recreated the audio output");
  require(std::abs(transport.snapshot().position_seconds - paused - 512.0 / 48000) < 1e-12,
          "Resume skipped or repeated source frames");
  fs::rename(folder.path / "moved-A.wav", folder.path / "A.wav");
  fs::rename(folder.path / "moved-B.wav", folder.path / "B.wav");
  transport.seek(0.1);
  require(device->advance(512).front() == 0, "Seek lost volume settings");
  transport.stop();
  transport.play();
  require(device->advance(512).front() == 0, "Restart lost volume settings");
  transport.pause();
  transport.set_track_volume(original[1].id, 1);
  transport.play();
  require(device->advance(512).front() == 0.25F, "Restoring original level failed");
  transport.seek(transport.snapshot().duration_seconds);
  transport.play();
  require(device->advance(512).front() == 0.25F, "Replay after end lost volumes");
  const auto tracks = transport.tracks();
  require(tracks[0].id == original[0].id && tracks[1].id == original[1].id &&
          tracks[0].volume == 0 && tracks[1].volume == 1, "Track identities or levels changed");
}
void exponential_smoothing() {
  for (const auto rate : {44100.0, 48000.0, 192000.0}) {
    TrackVolumes volumes(1);
    VolumeSmoother smoother(volumes, rate);
    smoother.begin_render();
    require(smoother.next_frame()[0] == 1, "Initial volume must not fade in");
    volumes.set(0, 0);
    smoother.begin_render();
    const auto frames = static_cast<std::size_t>(std::llround(rate * 0.005));
    double current = 1;
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const auto prior = current;
      current = smoother.next_frame()[0];
      const auto expected = std::exp(-static_cast<double>(frame + 1) / (rate * 0.005));
      require(std::abs(current - expected) < 1e-12 && current < prior,
              "Default smoothing differs from the expected 5ms exponential curve");
    }
    volumes.set(0, 0.8);
    smoother.begin_render();
    const auto coefficient = 1 - std::exp(-1 / (rate * 0.005));
    const auto expected = current + (0.8 - current) * coefficient;
    current = smoother.next_frame()[0];
    require(std::abs(current - expected) < 1e-12, "Retargeting restarted or jumped the exponential curve");
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(rate / 5); ++frame)
      current = smoother.next_frame()[0];
    require(current == 0.8, "Exponential smoothing did not settle exactly at a nonzero target");
    volumes.set(0, 0);
    smoother.begin_render();
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(rate / 5); ++frame)
      current = smoother.next_frame()[0];
    require(current == 0, "Exponential smoothing did not settle to exact silence");
    volumes.set(0, 1);
    smoother.begin_render();
    for (std::size_t frame = 0; frame < static_cast<std::size_t>(rate / 5); ++frame)
      current = smoother.next_frame()[0];
    require(current == 1, "Exponential smoothing did not settle at the original level");
  }
}
void concurrent_targets() {
  TrackVolumes volumes(maximum_tracks);
  VolumeSmoother ramp(volumes, 192000);
  std::jthread writer([&] {
    for (std::size_t i = 0; i < 20000; ++i)
      volumes.set(i % maximum_tracks, static_cast<double>(i % 101) / 100);
  });
  for (std::size_t i = 0; i < 2000; ++i) {
    ramp.begin_render();
    for (auto gain : ramp.next_frame())
      require(std::isfinite(gain) && gain >= -0.000001 && gain <= 1.000001,
              "Concurrent update produced an invalid gain");
  }
}
}
int main() {
  try {
    ramps_and_mixing();
    exponential_smoothing();
    transport_volumes(VolumeSmoothing::exponential);
    transport_volumes(VolumeSmoothing::linear);
    concurrent_targets();
    std::cout << "Independent track volumes, smoothing, persistence and concurrent updates passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
