#include "playback/buffering/stem_block.hpp"

#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace playset;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void settings_and_validation() {
  MixerControls controls(2);
  require(controls.master_volume() == 1 && !controls.master_muted(), "Wrong master defaults");
  controls.set(0, 0.25);
  controls.set_muted(1, true);
  controls.set_solo(0, true);
  controls.set_master_volume(0.5);
  controls.set_master_muted(true);
  for (const auto invalid : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
    bool rejected = false;
    try { controls.set_master_volume(invalid); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected && controls.master_volume() == 0.5 && controls.master_muted(),
            "Invalid master volume changed settings");
  }
  controls.set_master_volume(0);
  controls.set_master_volume(1);
  require(controls.master_volume() == 1 && controls.master_muted(),
          "Master endpoints were rejected or cleared mute");
  require(controls.get(0) == 0.25 && controls.get(1) == 1 && controls.is_muted(1) &&
              controls.is_solo(0) && !controls.is_solo(1),
          "Master edits changed track settings");
}

void mixing_and_clipping(VolumeSmoothing mode) {
  MixerControls controls(2);
  controls.set_master_volume(0.5);
  VolumeSmoother smoother(controls, 48000, mode);
  StemBlock block(2);
  block.frames = 1;
  for (std::size_t track = 0; track < 2; ++track) {
    block.track(track)[0] = 0.75F;
    block.track(track)[1] = -0.75F;
  }
  std::array<float, 2> output{};
  smoother.begin_render();
  require(mix_stem_block(block, 0, output, &smoother) == 0 &&
              output[0] == 0.75F && output[1] == -0.75F,
          "Master volume was applied after clipping or twice per stereo frame");
  require(block.track(0)[0] == 0.75F, "Mixing changed source samples");

  controls.set_master_volume(1);
  smoother.settle();
  require(mix_stem_block(block, 0, output, &smoother) == 2 &&
              output[0] == 1 && output[1] == -1, "Output overloads were not counted");
  controls.set_master_muted(true);
  smoother.settle();
  require(mix_stem_block(block, 0, output, &smoother) == 0 &&
              output[0] == 0 && output[1] == 0, "Master mute leaked audio or counted clipping");
  controls.set_master_volume(0.25);
  smoother.settle();
  mix_stem_block(block, 0, output, &smoother);
  require(output[0] == 0 && controls.master_volume() == 0.25,
          "Editing volume while master-muted made audio audible");
  controls.set_master_muted(false);
  smoother.settle();
  mix_stem_block(block, 0, output, &smoother);
  require(output[0] == 0.375F && output[1] == -0.375F,
          "Master unmute did not restore the edited volume");
  require(mix_stem_block(block, 0, output) == 2 && output[0] == 1,
          "Mixing without controls changed its default behavior");

  controls.set_solo(0, true);
  controls.set_master_volume(1);
  smoother.settle();
  controls.set_master_volume(0);
  smoother.begin_render();
  mix_stem_block(block, 0, output, &smoother);
  const auto first_gain = mode == VolumeSmoothing::exponential
      ? std::exp(-1 / (48000 * 0.005)) : 1 - 1.0 / 240;
  require(output[0] == static_cast<float>(0.75 * first_gain) && output[1] == -output[0],
          "Stereo channels advanced the master fade separately");
  smoother.settle();
  mix_stem_block(block, 0, output, &smoother);
  require(output[0] == 0, "Solo bypassed zero master volume");
  controls.set_master_volume(1);
  controls.set_master_muted(true);
  smoother.settle();
  mix_stem_block(block, 0, output, &smoother);
  require(output[0] == 0, "Solo bypassed master mute");
}

void smoothing(double rate, VolumeSmoothing mode) {
  MixerControls controls(1);
  VolumeSmoother smoother(controls, rate, mode);
  smoother.begin_render();
  controls.set(0, 0);
  controls.set_master_volume(0);
  smoother.begin_render();
  const auto frames = static_cast<std::size_t>(std::llround(rate * 0.005));
  double current = 1;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto previous = current;
    const auto tracks = smoother.next_frame();
    current = smoother.master_gain();
    const auto expected = mode == VolumeSmoothing::exponential
        ? std::exp(-static_cast<double>(frame + 1) / (rate * 0.005))
        : 1 - static_cast<double>(frame + 1) / frames;
    require(std::abs(current - expected) < 1e-12 && current < previous && tracks[0] == current,
            "Master smoothing differs from the selected curve or track smoothing");
    require(smoother.master_gain() == current, "Reading master gain advanced smoothing");
  }
  controls.set_master_volume(0.8);
  smoother.begin_render();
  const auto expected = current + (0.8 - current) *
      (mode == VolumeSmoothing::exponential ? 1 - std::exp(-1 / (rate * 0.005)) : 1.0 / frames);
  smoother.next_frame();
  require(std::abs(smoother.master_gain() - expected) < 1e-12,
          "Retargeting master volume jumped or restarted smoothing");
  controls.set_master_muted(true);
  smoother.begin_render();
  for (std::size_t frame = 0; frame < static_cast<std::size_t>(rate / 5); ++frame)
    smoother.next_frame();
  require(smoother.master_gain() == 0, "Master mute did not reach exact silence");
  controls.set_master_muted(false);
  smoother.begin_render();
  smoother.next_frame();
  require(smoother.master_gain() > 0 && smoother.master_gain() < 0.8,
          "Live master unmute skipped smoothing");
  smoother.settle();
  require(smoother.master_gain() == 0.8, "Settling did not restore master volume immediately");
  require(smoother.next_frame()[0] == 0, "Master changes restarted the settled track ramp");

  MixerControls initially_muted(1);
  initially_muted.set_master_muted(true);
  VolumeSmoother initial(initially_muted, rate, mode);
  initial.begin_render();
  initial.next_frame();
  require(initial.master_gain() == 0, "Initially muted master faded from unity");
}

void concurrent_updates() {
  MixerControls controls(maximum_tracks);
  VolumeSmoother smoother(controls, 48000);
  smoother.settle();
  std::jthread writer([&] {
    for (std::size_t i = 0; i < 20000; ++i) {
      controls.set_master_volume(static_cast<double>(i % 101) / 100);
      controls.set_master_muted(i % 2 == 0);
      controls.set(i % maximum_tracks, 0.25);
    }
    controls.set_master_volume(0.75);
    controls.set_master_muted(false);
  });
  for (std::size_t i = 0; i < 2000; ++i) {
    smoother.begin_render();
    const auto tracks = smoother.next_frame();
    require(tracks.size() == maximum_tracks && std::isfinite(smoother.master_gain()) &&
                smoother.master_gain() >= 0 && smoother.master_gain() <= 1,
            "Concurrent controls produced invalid master output");
  }
  writer.join();
  smoother.settle();
  require(smoother.master_gain() == 0.75, "Completed master update was not adopted");
}
}

int main() {
  try {
    settings_and_validation();
    for (const auto mode : {VolumeSmoothing::exponential, VolumeSmoothing::linear}) {
      mixing_and_clipping(mode);
      for (const auto rate : {8000.0, 44100.0, 48000.0, 192000.0}) smoothing(rate, mode);
    }
    concurrent_updates();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
