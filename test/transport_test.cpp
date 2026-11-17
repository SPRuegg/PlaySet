#include "playback/transport.hpp"
#include "fixture_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace playset;
using namespace playset::test;

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
struct OfflineOutput final : PlaybackOutput {
  BufferedPlayback &playback;
  bool running = false;
  std::uint64_t frame = 0;
  explicit OfflineOutput(BufferedPlayback &source) : playback(source) {}
  void play() override { running = true; }
  void pause() override { running = false; }
  std::uint64_t position_frames() const override { return frame; }
  bool finished() override { return playback.exhausted(); }
  std::vector<float> advance(std::size_t frames) {
    std::vector<float> output(frames * output_channels, 0);
    if (running) frame += playback.render(output);
    return output;
  }
};
void reader_seek(const fs::path &file) {
  auto reader = StemReader::open(file);
  std::vector<float> reference;
  std::array<float, decode_block_frames * output_channels> block{};
  for (auto frames = reader.read(block); frames; frames = reader.read(block))
    reference.insert(reference.end(), block.begin(), block.begin() + frames * output_channels);
  const auto length = reference.size() / output_channels;
  for (const auto frame : {std::size_t{0}, length / 3, length - 1, length, length / 2, std::size_t{0}}) {
    reader.seek(frame);
    const auto count = reader.read(block);
    require(count == std::min(decode_block_frames, length - frame), "Seek frame count differs");
    for (std::size_t i = 0; i < count * output_channels; ++i)
      require(std::abs(block[i] - reference[frame * output_channels + i]) < 0.00001F,
              "Seek samples differ from sequential decode");
    require(std::all_of(block.begin() + count * output_channels, block.end(),
                        [](float value) { return value == 0; }), "Seek tail is not silent");
  }
}
void transport_controls() {
  TempFolder folder;
  write_pcm_wav(folder.path / "long.wav", 44100, 1, 16, 20000);
  write_pcm_wav(folder.path / "short.wav", 44100, 1, 16, 2000);
  OfflineOutput *device = nullptr;
  auto factory = [&](BufferedPlayback &playback, double rate) {
    require(rate == 44100, "Output sample rate changed");
    auto output = std::make_unique<OfflineOutput>(playback);
    device = output.get();
    return output;
  };
  Transport transport(prepare_stems(folder.path), factory);
  require(transport.snapshot().status == TransportStatus::stopped, "Must load stopped");
  device->advance(512);
  require(transport.snapshot().position_seconds == 0, "Stopped playback advanced");
  transport.play();
  auto audio = device->advance(512);
  require(audio.front() == 0.5F, "Stems did not start together");
  transport.pause();
  const auto paused = transport.snapshot().position_seconds;
  require(paused == 512.0 / 44100, "Pause did not retain output position");
  audio = device->advance(512);
  require(audio.front() == 0 && transport.snapshot().position_seconds == paused, "Pause advanced or sounded");
  transport.pause();
  transport.play();
  transport.play();
  device->advance(512);
  require(transport.snapshot().position_seconds == 1024.0 / 44100, "Resume lost position");
  transport.seek(5000.0 / 44100);
  require(transport.snapshot().status == TransportStatus::playing, "Playing seek stopped playback");
  audio = device->advance(512);
  require(audio.front() == 0.25F, "Short stem was not silent after its end");
  require(transport.snapshot().position_seconds == 5512.0 / 44100, "Playing seek used stale queued audio");
  transport.pause();
  transport.seek(0);
  require(transport.snapshot().status == TransportStatus::paused, "Paused seek started playback");
  transport.play();
  audio = device->advance(512);
  require(audio.front() == 0.5F, "Backward seek did not restore short stem");
  transport.stop();
  require(transport.snapshot().status == TransportStatus::stopped && transport.snapshot().position_seconds == 0,
          "Stop did not reset transport");
  transport.seek(1.0 / 44100);
  require(transport.snapshot().status == TransportStatus::stopped && transport.snapshot().position_seconds == 1.0 / 44100,
          "Stopped seek did not remain stopped at requested frame");
  for (double invalid : {-1.0, 1.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
    bool rejected = false;
    try { transport.seek(invalid); } catch (const std::invalid_argument &) { rejected = true; }
    require(rejected && transport.snapshot().position_seconds == 1.0 / 44100, "Invalid seek changed state");
  }
  transport.seek(transport.snapshot().duration_seconds);
  require(transport.snapshot().position_seconds == 20000.0 / 44100, "Exact end was rejected");
  transport.play();
  require(transport.snapshot().position_seconds == 0, "Play at end did not restart");
  transport.seek(transport.snapshot().duration_seconds);
  require(transport.snapshot().status == TransportStatus::ended, "Playing seek to end must finish");
  transport.play();
  transport.seek(19999.0 / 44100);
  audio = device->advance(512);
  require(audio.front() == 0.25F && audio[2] == 0, "Final partial block incorrect");
  device->advance(512);
  require(transport.snapshot().status == TransportStatus::ended, "End was not reported");
  transport.seek(0);
  require(transport.snapshot().status == TransportStatus::paused, "Seek after end must stay silent");
  transport.play();
  transport.stop();
  transport.stop();
  require(transport.snapshot().position_seconds == 0, "Repeated stop failed");

  fs::rename(folder.path / "long.wav", folder.path / "moved.wav");
  bool failed = false;
  try { transport.seek(0.1); } catch (const std::exception &) { failed = true; }
  require(failed && transport.snapshot().position_seconds == 0, "Failed seek replaced active session");
  transport.stop();
  require(transport.snapshot().status == TransportStatus::stopped, "Stop depended on source path");
}
}
int main(int argc, char **argv) {
  try {
    if (argc != 3) throw std::runtime_error("Expected AAC and ALAC fixture paths");
    TempFolder folder;
    write_pcm_wav(folder.path / "tone.wav", 44100, 2, 16, 11025);
    {
      std::fstream wav(folder.path / "tone.wav", std::ios::in | std::ios::out | std::ios::binary);
      wav.seekp(44);
      for (unsigned frame = 0; frame < 11025; ++frame) {
        for (unsigned channel = 0; channel < 2; ++channel) {
          const auto value = (frame * 13 + channel * 7) % 65536;
          wav.put(static_cast<char>(value & 255));
          wav.put(static_cast<char>(value >> 8));
        }
      }
      require(wav.good(), "Could not generate varying seek fixture");
    }
    reader_seek(folder.path / "tone.wav");
    reader_seek(argv[1]);
    reader_seek(argv[2]);
    transport_controls();
    std::cout << "Transport controls and WAV/AAC/ALAC seeking passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
