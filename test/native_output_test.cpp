#include "muted_audio_output.hpp"
#undef AudioOutputUnitStart

#include "fixture_support.hpp"
#include "playback/transport/transport.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace playset;
using namespace playset::test;
using namespace std::chrono_literals;
unsigned muted_starts = 0;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, "Native audio output timed out");
    std::this_thread::sleep_for(2ms);
  }
}
void check_rate(std::uint32_t rate) {
  TempFolder folder;
  const auto source = folder.path / "tone.wav";
  const auto moved = folder.path / "moved.wav";
  write_pcm_wav(source, rate, 1, 16, rate * 2);
  unsigned outputs_created = 0;
  auto factory = [&](BufferedPlayback &playback, double sample_rate) {
    ++outputs_created;
    return make_audio_output(playback, sample_rate);
  };
  Transport transport(prepare_stems(folder.path), factory);
  const auto starts_before = muted_starts;
  transport.seek(0.25);
  require(transport.snapshot().status == TransportStatus::stopped &&
              transport.snapshot().position_seconds == 0.25,
          "Seek before first Play started playback or missed its boundary");
  std::this_thread::sleep_for(20ms);
  require(transport.snapshot().position_seconds == 0.25, "Stopped seek advanced");
  transport.set_track_volume(0, 0.5);
  transport.play();
  until([&] { return transport.snapshot().position_seconds > 0.25; });
  transport.pause();
  const auto paused = transport.snapshot().position_seconds;
  std::this_thread::sleep_for(30ms);
  require(transport.snapshot().position_seconds == paused, "Paused native output advanced");
  fs::rename(source, moved);
  transport.seek(0.5);
  require(transport.snapshot().position_seconds == 0.5 &&
              transport.snapshot().status == TransportStatus::paused,
          "Native paused bank switch changed playback state");
  transport.set_track_muted(0, true);
  require(transport.tracks()[0].muted && transport.tracks()[0].volume == 0.5,
          "Native paused mute lost its volume setting");
  transport.play();
  until([&] { return transport.snapshot().position_seconds > 0.5; });
  transport.pause();
  require(outputs_created == 1 && muted_starts == starts_before + 1,
          "Native resume recreated or restarted the output");
  transport.play();
  transport.seek(1.0);
  require(transport.snapshot().position_seconds >= 1.0 &&
              transport.snapshot().status == TransportStatus::playing,
          "Playing bank switch did not keep playback running");
  transport.pause();
  fs::rename(moved, source);
  transport.seek(1.95);
  transport.play();
  until([&] { return transport.snapshot().status == TransportStatus::ended; });
  transport.play();
  until([&] { return transport.snapshot().position_seconds > 0; });
  transport.stop();
  require(transport.snapshot().position_seconds == 0, "Native stop did not reset the position");
  require(outputs_created == 1 && muted_starts == starts_before + 1,
          "Seek, Stop or replay recreated or restarted native output");
  std::cout << "Muted native resume and transport passed at " << rate << " Hz\n";
}
}

OSStatus playset_test_start_muted(AudioUnit unit) {
  auto status = AudioUnitSetParameter(unit, kHALOutputParam_Volume,
                                     kAudioUnitScope_Global, 0, 0, 0);
  if (status != noErr) return status;
  AudioUnitParameterValue volume = 1;
  status = AudioUnitGetParameter(unit, kHALOutputParam_Volume,
                                kAudioUnitScope_Global, 0, &volume);
  if (status != noErr || volume != 0) return kAudio_ParamError;
  ++muted_starts;
  return AudioOutputUnitStart(unit);
}

int main() {
  try {
    for (const auto rate : {8000U, 44100U, 48000U, 96000U, 192000U}) check_rate(rate);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
