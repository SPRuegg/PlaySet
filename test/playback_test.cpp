#include "fixture_support.hpp"
#include "playback/buffering/buffered_playback.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>

namespace {
using namespace playset;
using namespace playset::test;
using namespace std::chrono_literals;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void mixing() {
  StemBlock block(3);
  block.frames = 2;
  for (std::size_t track = 0; track < block.track_count(); ++track) {
    auto samples = block.track(track);
    samples[0] = track == 2 ? -0.75F : 0.75F;
    samples[1] = -0.75F;
    samples[2] = 0.25F;
    samples[3] = 0.25F;
  }
  std::array<float, 4> output{};
  require(mix_stem_block(block, 0, output) == 1,
          "Count clipping per output channel sample");
  require(output[0] == 0.75F, "Preserve headroom until the final sum; do not "
                              "clip individual contributions");
  require(output[1] == -1.0F && output[2] == 0.75F && output[3] == 0.75F,
          "Final output clamp");
  block.track(2)[0] = 0.75F;
  require(mix_stem_block(block, 0, output) == 2 && output[0] == 1.0F,
          "Count and clamp positive overloads too");
  // This cancellation loses the small contribution with a float accumulator.
  block.track(0)[0] = 16777216.0F;
  block.track(1)[0] = 0.25F;
  block.track(2)[0] = -16777216.0F;
  mix_stem_block(block, 0, output);
  require(output[0] == 0.25F, "Sum with double precision");
}

void decoding() {
  TempFolder temp;
  write_pcm_wav(temp.path / "long.wav", 44100, 2, 16, 1701);
  write_pcm_wav(temp.path / "short.wav", 44100, 1, 16, 777);
  auto song = prepare_stems(temp.path);
  StemBlock block(song.stems.size());
  std::array<float, playback_block_frames * output_channels> output{};
  std::size_t position = 0;
  while (const auto frames = decode_stem_block(song, block)) {
    auto active = std::span(output).first(frames * output_channels);
    require(mix_stem_block(block, 0, active) == 0,
            "No overload in fixture mix");
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const bool both = position + frame < 777;
      require(output[frame * 2] == (both ? 0.5F : 0.25F),
              "Both tracks start together; short track pads with silence");
      require(output[frame * 2 + 1] == (both ? -0.25F : -0.5F),
              "Preserve stereo channel alignment");
    }
    position += frames;
  }
  require(
      position == 1701,
      "Finish exactly at the longest stem, including the final partial block");
  require(decode_stem_block(song, block) == 0, "EOF stays stable");
}

void queue() {
  StemBlockQueue queue(1);
  std::array<float, 2> first{};
  first.fill(1);
  require(queue.render(first).frames == 0 && first[0] == 0 && first[1] == 0,
          "Underrun produces silence");
  for (unsigned cycle = 0; cycle < 30; ++cycle) {
    for (std::size_t index = 0; index < StemBlockQueue::capacity; ++index) {
      auto *block = queue.begin_write();
      require(block != nullptr, "Free slot available");
      block->frames = 5;
      std::fill(block->track(0).begin(), block->track(0).end(),
                static_cast<float>(index) / 16);
      queue.commit_write();
    }
    require(queue.begin_write() == nullptr,
            "Full queue must not overwrite unread audio");
    for (std::size_t index = 0; index < StemBlockQueue::capacity; ++index) {
      std::array<float, 6> part{};
      require(queue.render(part).frames == 3, "Partial block consumed");
      for (float value : part)
        require(value == static_cast<float>(index) / 16,
                "Queue ordering after wraparound");
      std::array<float, 4> rest{};
      require(queue.render(rest).frames == 2,
              "Retain unread frames within a block");
      for (float value : rest)
        require(value == static_cast<float>(index) / 16,
                "Partial-read continuity");
    }
    require(queue.ready_blocks() == 0, "All slots released");
  }
}

void concurrent_queue() {
  StemBlockQueue queue(1);
  constexpr std::size_t blocks = 2000;
  std::jthread producer([&](std::stop_token stop) {
    for (std::size_t index = 0; index < blocks && !stop.stop_requested();
         ++index) {
      StemBlock *block;
      while (!(block = queue.begin_write())) {
        if (stop.stop_requested())
          return;
        std::this_thread::yield();
      }
      block->frames = playback_block_frames;
      for (std::size_t frame = 0; frame < playback_block_frames; ++frame) {
        const auto value =
            static_cast<float>((index * playback_block_frames + frame) % 1024) /
            2048;
        block->track(0)[frame * 2] = value;
        block->track(0)[frame * 2 + 1] = -value;
      }
      queue.commit_write();
    }
  });
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  std::size_t position = 0;
  std::array<float, 346> output{};
  while (position < blocks * playback_block_frames) {
    require(std::chrono::steady_clock::now() < deadline,
            "Concurrent queue made progress");
    const auto result = queue.render(output);
    for (std::size_t frame = 0; frame < result.frames; ++frame) {
      const auto expected =
          static_cast<float>((position + frame) % 1024) / 2048;
      require(output[frame * 2] == expected &&
                  output[frame * 2 + 1] == -expected,
              "Concurrent producer/consumer preserves every source frame");
    }
    position += result.frames;
    if (!result.frames)
      std::this_thread::yield();
  }
  require(position == blocks * playback_block_frames,
          "No duplicated or missing frames");
}

void buffered(const std::filesystem::path &fixture) {
  {
    TempFolder short_song;
    write_pcm_wav(short_song.path / "short.wav", 44100, 1, 16, 100);
    BufferedPlayback playback(prepare_stems(short_song.path));
    playback.wait_until_ready();
    require(playback.render({}) == 0 && !playback.exhausted(),
            "An empty render does not discard queued audio or signal EOF");
    std::array<float, 200> output{};
    require(playback.render(output) == 100, "Retain a song shorter than one output buffer");
    require(playback.render(output) == 0 && playback.exhausted(), "Exhaust only after consuming the final frame");
  }
  TempFolder temp;
  std::filesystem::copy_file(fixture, temp.path / "first.m4a");
  std::filesystem::copy_file(fixture, temp.path / "second.m4a");
  BufferedPlayback playback(prepare_stems(temp.path));
  playback.wait_until_ready();
  std::array<float, 254> output{};
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!playback.exhausted()) {
    require(std::chrono::steady_clock::now() < deadline,
            "Buffered playback reached end");
    playback.render(output);
    std::this_thread::sleep_for(1ms);
  }
  playback.rethrow_if_failed();
  const auto report = playback.report();
  require(report.rendered_frames == 11025,
          "Two stems play one shared timeline, not consecutively");
  require(report.clipped_samples == 0, "Fixture mix does not overload");
  // Early destruction must join the producer even if it fills the queue.
  auto prepared_song = prepare_stems(temp.path);
  BufferedPlayback abandoned(std::move(prepared_song));
  abandoned.wait_until_ready();
}

void decoder_error() {
  TempFolder temp;
  const auto path = temp.path / "nonfinite.wav";
  std::ofstream out(path, std::ios::binary);
  constexpr std::uint32_t frames = 9000;
  out.write("RIFF", 4);
  write_little_endian(out, 36 + frames * 4, 4);
  out.write("WAVEfmt ", 8);
  write_little_endian(out, 16, 4);
  write_little_endian(out, 3, 2);
  write_little_endian(out, 1, 2);
  write_little_endian(out, 44100, 4);
  write_little_endian(out, 44100 * 4, 4);
  write_little_endian(out, 4, 2);
  write_little_endian(out, 32, 2);
  out.write("data", 4);
  write_little_endian(out, frames * 4, 4);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const float value =
        frame == 5000 ? std::numeric_limits<float>::quiet_NaN() : 0.25F;
    write_little_endian(out, std::bit_cast<std::uint32_t>(value), 4);
  }
  out.close();
  auto song = prepare_stems(temp.path);
  bool rejected = false;
  try {
    BufferedPlayback playback(std::move(song));
    playback.wait_until_ready();
    std::array<float, 1024> output{};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!playback.exhausted()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Decoder error did not hang playback");
      playback.render(output);
      std::this_thread::sleep_for(1ms);
    }
    playback.rethrow_if_failed();
  } catch (const std::runtime_error &error) {
    rejected =
        std::string(error.what()).find("non-finite") != std::string::npos;
  }
  require(rejected, "Background decoder errors reach the control thread");
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "Expected test name and fixture path");
    const std::string name = argv[1];
    if (name == "mixing")
      mixing();
    else if (name == "decoding")
      decoding();
    else if (name == "queue")
      queue();
    else if (name == "concurrent")
      concurrent_queue();
    else if (name == "buffered")
      buffered(argv[2]);
    else if (name == "error")
      decoder_error();
    else
      throw std::runtime_error("Unknown test");
    std::cout << "PASS: " << name << '\n';
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
