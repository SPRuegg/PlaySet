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
  std::int64_t frame = 0;
  explicit OfflineOutput(BufferedPlayback &source) : playback(source) {}
  void play() override { playback.settle_volumes(); running = true; }
  void pause() override { running = false; }
  void seek(std::int64_t target) override {
    playback.prepare_seek(target);
    const auto origin = playback.apply_pending_seek();
    require(origin.has_value(), "Prepared seek was not available at boundary");
    frame = *origin;
    playback.acknowledge_seek();
  }
  std::int64_t position_frames() const override { return frame; }
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
void mute_controls(VolumeSmoothing mode) {
  TrackVolumes volumes(2);
  require(!volumes.is_muted(0) && !volumes.is_muted(1), "Tracks must load unmuted");
  volumes.set(0, 0.5);
  VolumeSmoother smoother(volumes, 48000, mode);
  smoother.begin_render();
  require(smoother.next_frame()[0] == 0.5, "Initial level changed");
  volumes.set_muted(0, true);
  smoother.begin_render();
  auto levels = smoother.next_frame();
  require(levels[0] > 0 && levels[0] < 0.5 && levels[1] == 1,
          "Live mute jumped or changed a different track");
  for (int frame = 0; frame < 9600; ++frame) levels = smoother.next_frame();
  require(levels[0] == 0 && volumes.get(0) == 0.5,
          "Mute did not reach silence or overwrote stored volume");
  volumes.set(0, 0.75);
  smoother.settle();
  require(smoother.next_frame()[0] == 0 && volumes.is_muted(0),
          "Editing muted volume made the track audible");
  const std::array<double, 2> batch{0.25, 0.5};
  volumes.set_all(batch);
  smoother.settle();
  levels = smoother.next_frame();
  require(levels[0] == 0 && levels[1] == 0.5, "Volume batch cleared mute");
  volumes.set_muted(0, false);
  smoother.begin_render();
  levels = smoother.next_frame();
  require(levels[0] > 0 && levels[0] < 0.25,
          "Live unmute did not ramp toward saved level");
  smoother.settle();
  require(smoother.next_frame()[0] == 0.25, "Unmute lost edited volume");
  for (bool muted : {true, false}) {
    bool rejected = false;
    try { volumes.set_muted(2, muted); } catch (const std::out_of_range &) { rejected = true; }
    require(rejected && !volumes.is_muted(0), "Invalid mute ID changed state");
  }
  bool rejected = false;
  try { static_cast<void>(volumes.is_muted(2)); } catch (const std::out_of_range &) { rejected = true; }
  require(rejected, "Mute getter accepted an invalid ID");
}

void batch_mutes() {
  TrackVolumes volumes(3);
  const std::array<TrackId, 2> selected{0, 2};
  volumes.set_muted(selected, true);
  require(volumes.is_muted(0) && !volumes.is_muted(1) && volumes.is_muted(2),
          "Batch mute changed unselected tracks");
  const std::array<TrackId, 2> invalid{1, 3};
  bool rejected = false;
  try { volumes.set_muted(invalid, true); } catch (const std::out_of_range &) { rejected = true; }
  require(rejected && !volumes.is_muted(1), "Invalid batch partly changed mute state");
  volumes.set_muted(std::span<const TrackId>{}, false);
  require(volumes.is_muted(0), "Empty mute batch changed state");
  const std::array<TrackId, 3> duplicates{0, 2, 0};
  volumes.set_muted(duplicates, false);
  require(!volumes.is_muted(0) && !volumes.is_muted(2), "Batch unmute failed");

  TrackVolumes grouped(maximum_tracks);
  std::array<TrackId, maximum_tracks> ids{};
  for (TrackId id = 0; id < ids.size(); ++id) ids[id] = id;
  VolumeSmoother smoother(grouped, 48000);
  smoother.settle();
  std::atomic<bool> done{false};
  std::jthread writer([&] {
    for (int i = 0; i < 20000; ++i) grouped.set_muted(ids, i % 2 == 0);
    done.store(true);
  });
  do {
    smoother.settle();
    const auto gains = smoother.next_frame();
    for (auto gain : gains)
      require(gain == gains.front(), "Playback adopted only part of a mute batch");
  } while (!done.load());
  writer.join();
  smoother.settle();
  require(smoother.next_frame()[0] == 1, "Final unmute batch was not adopted");
}

void transport_mutes(VolumeSmoothing mode) {
  TempFolder folder;
  write_pcm_wav(folder.path / "A.wav", 48000, 1, 16, 48000);
  write_pcm_wav(folder.path / "B.wav", 48000, 1, 16, 48000);
  OfflineOutput *device = nullptr;
  std::size_t created = 0;
  auto factory = [&](BufferedPlayback &playback, double) {
    ++created;
    auto output = std::make_unique<OfflineOutput>(playback);
    device = output.get();
    return output;
  };
  Transport transport(prepare_stems(folder.path), factory, mode);
  transport.set_track_muted(0, true);
  transport.play();
  require(device->advance(512).front() == 0.25F, "Pre-play mute leaked audio");
  transport.pause();
  const auto paused = transport.snapshot().position_seconds;
  const std::array<TrackId, 2> both{0, 1};
  transport.set_tracks_muted(both, true);
  transport.set_track_volume(0, 0.5);
  require(transport.snapshot().position_seconds == paused, "Paused mute moved position");
  transport.play();
  require(device->advance(512).front() == 0, "Paused mute leaked audio on resume");
  transport.seek(0.1);
  require(device->advance(512).front() == 0, "Seek lost mute state");
  transport.stop();
  transport.play();
  require(device->advance(512).front() == 0, "Stop lost mute state");
  transport.seek(transport.snapshot().duration_seconds);
  transport.play();
  require(device->advance(512).front() == 0, "Replay lost mute state");
  transport.pause();
  transport.set_track_muted(0, false);
  transport.play();
  require(device->advance(512).front() == 0.125F, "Unmute did not restore edited level");
  const auto tracks = transport.tracks();
  require(!tracks[0].muted && tracks[1].muted && tracks[0].volume == 0.5 && tracks[1].volume == 1,
          "Track snapshot lost mute state or requested volume");
  require(created == 1, "Mute recreated output");
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
    for (std::size_t i = 0; i < 20000; ++i) {
      volumes.set(i % maximum_tracks, static_cast<double>(i % 101) / 100);
      volumes.set_muted(i % maximum_tracks, i % 2 == 0);
    }
  });
  for (std::size_t i = 0; i < 2000; ++i) {
    ramp.begin_render();
    for (auto gain : ramp.next_frame())
      require(std::isfinite(gain) && gain >= -0.000001 && gain <= 1.000001,
              "Concurrent update produced an invalid gain");
  }
}
void coherent_batches() {
  TrackVolumes volumes(maximum_tracks);
  VolumeSmoother smoother(volumes, 48000);
  std::array<double, maximum_tracks> batch{};
  batch.fill(0.25);
  volumes.set_all(batch);
  smoother.begin_render();
  require(smoother.next_frame()[0] == 0.25, "Initial batch faded in");
  batch.back() = std::numeric_limits<double>::quiet_NaN();
  bool rejected = false;
  try { volumes.set_all(batch); } catch (const std::invalid_argument &) { rejected = true; }
  require(rejected, "Invalid batch was accepted");
  for (TrackId id = 0; id < volumes.size(); ++id)
    require(volumes.get(id) == 0.25, "Invalid batch partially changed targets");
  rejected = false;
  try { volumes.set_all(std::span(batch).first(1)); }
  catch (const std::invalid_argument &) { rejected = true; }
  require(rejected, "Wrong-size batch was accepted");

  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::jthread writer([&] {
    while (!start.load()) std::this_thread::yield();
    for (std::size_t i = 0; i < 20000; ++i) {
      batch.fill(i % 2 ? 0.75 : 0.125);
      volumes.set_all(batch);
    }
    done.store(true);
  });
  start.store(true);
  do {
    smoother.settle();
    const auto levels = smoother.next_frame();
    for (const auto level : levels)
      require(level == levels.front(), "Playback adopted a partial volume batch");
  } while (!done.load());
  writer.join();
  smoother.settle();
  for (const auto level : smoother.next_frame())
    require(level == 0.75, "Completed batch was not adopted");
  volumes.set(0, 0.5);
  smoother.settle();
  const auto levels = smoother.next_frame();
  require(levels[0] == 0.5 && levels[1] == 0.75,
          "Single-track update changed another track");
}
}
int main() {
  try {
    ramps_and_mixing();
    batch_mutes();
    for (auto mode : {VolumeSmoothing::exponential, VolumeSmoothing::linear}) {
      mute_controls(mode);
      transport_mutes(mode);
    }
    exponential_smoothing();
    transport_volumes(VolumeSmoothing::exponential);
    transport_volumes(VolumeSmoothing::linear);
    concurrent_targets();
    coherent_batches();
    std::cout << "Independent track volumes, smoothing, persistence and concurrent updates passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
