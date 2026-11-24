#include "playback/output/audio_output.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace playset {
namespace {
using namespace std::chrono_literals;
constexpr auto output_timeout = 10s;

void check(OSStatus status, const char *operation) {
  if (status != noErr)
    throw std::runtime_error(std::string(operation) + " (AudioUnit " +
                             std::to_string(status) + ")");
}

struct OutputState {
  BufferedPlayback &playback;
  double ticks_per_frame;
  std::uint64_t latency_ticks = 0;
  std::atomic<OSStatus> error{noErr};
  std::atomic<std::uint64_t> command{0};
  std::atomic<std::uint64_t> observed_command{0};
  std::atomic<std::int64_t> frames{0};
  std::atomic<std::uint64_t> audio_end_tick{0};
  bool was_playing = false;
};
static_assert(std::atomic<OSStatus>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);

OSStatus render(void *user, AudioUnitRenderActionFlags *flags,
                const AudioTimeStamp *time, UInt32, UInt32 frame_count,
                AudioBufferList *buffers) noexcept {
  auto &state = *static_cast<OutputState *>(user);
  if (frame_count > StemBlockQueue::maximum_render_frames ||
      !buffers || buffers->mNumberBuffers != 1 ||
      !buffers->mBuffers[0].mData ||
      buffers->mBuffers[0].mNumberChannels != output_channels ||
      buffers->mBuffers[0].mDataByteSize / sizeof(float) / output_channels < frame_count) {
    state.error.store(kAudio_ParamError, std::memory_order_relaxed);
    return kAudio_ParamError;
  }
  auto output = std::span(static_cast<float *>(buffers->mBuffers[0].mData),
                          static_cast<std::size_t>(frame_count) * output_channels);
  const auto command = state.command.load(std::memory_order_acquire);
  const bool playing = (command & 1) != 0;
  if (!playing || !state.was_playing) state.playback.settle_volumes();
  std::size_t frames = 0;
  if (playing) frames = state.playback.render(output);
  else std::fill(output.begin(), output.end(), 0.0F);
  if (frames == 0) *flags |= kAudioUnitRenderAction_OutputIsSilence;
  else *flags &= ~kAudioUnitRenderAction_OutputIsSilence;
  if (frames > 0) {
    state.frames.fetch_add(static_cast<std::int64_t>(frames), std::memory_order_relaxed);
    const auto start = time && (time->mFlags & kAudioTimeStampHostTimeValid)
                           ? time->mHostTime : mach_absolute_time();
    state.audio_end_tick.store(start + static_cast<std::uint64_t>(
        std::ceil(frames * state.ticks_per_frame)) + state.latency_ticks,
        std::memory_order_release);
  }
  state.was_playing = playing;
  state.observed_command.store(command, std::memory_order_release);
  return noErr;
}

struct UnitOwner {
  AudioUnit unit = nullptr;
  ~UnitOwner() {
    if (unit) {
      AudioOutputUnitStop(unit);
      AudioUnitUninitialize(unit);
      AudioComponentInstanceDispose(unit);
    }
  }
};

class MacAudioOutput final : public PlaybackOutput {
public:
  MacAudioOutput(BufferedPlayback &playback, double sample_rate)
      : state_{playback, ticks_per_second() / sample_rate} {
    AudioComponentDescription description{};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    const auto component = AudioComponentFindNext(nullptr, &description);
    if (!component) throw std::runtime_error("Could not find the default audio output.");
    check(AudioComponentInstanceNew(component, &owner_.unit), "Could not create audio output");
    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mChannelsPerFrame = output_channels;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = output_channels * sizeof(float);
    format.mBytesPerPacket = format.mBytesPerFrame;
    check(AudioUnitSetProperty(owner_.unit, kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Input, 0, &format, sizeof(format)),
          "Could not configure audio output");
    AURenderCallbackStruct callback{render, &state_};
    check(AudioUnitSetProperty(owner_.unit, kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input, 0, &callback, sizeof(callback)),
          "Could not install audio renderer");
    check(AudioUnitInitialize(owner_.unit), "Could not initialize audio output");
    Float64 latency = 0;
    UInt32 size = sizeof(latency);
    check(AudioUnitGetProperty(owner_.unit, kAudioUnitProperty_Latency,
                               kAudioUnitScope_Global, 0, &latency, &size),
          "Could not inspect output latency");
    if (!std::isfinite(latency) || latency < 0)
      throw std::runtime_error("Audio output returned an invalid latency.");
    state_.latency_ticks = static_cast<std::uint64_t>(std::ceil(latency * ticks_per_second()));
  }

  void play() override {
    check_error();
    const auto command = state_.command.load(std::memory_order_relaxed);
    if ((command & 1) != 0) return;
    state_.command.store((command + 2) | 1, std::memory_order_release);
    if (!started_) {
      const auto status = AudioOutputUnitStart(owner_.unit);
      if (status != noErr) {
        state_.command.store(command, std::memory_order_release);
        check(status, "Could not start audio output");
      }
      started_ = true;
    }
  }
  void pause() override {
    const auto command = state_.command.load(std::memory_order_relaxed);
    if ((command & 1) == 0) return;
    const auto paused = (command + 2) & ~std::uint64_t{1};
    state_.command.store(paused, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + output_timeout;
    while (state_.observed_command.load(std::memory_order_acquire) != paused) {
      check_error();
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Audio output did not acknowledge pause.");
      std::this_thread::sleep_for(1ms);
    }
  }
  std::int64_t position_frames() const override {
    return state_.frames.load(std::memory_order_relaxed);
  }
  bool finished() override {
    check_error();
    return state_.playback.exhausted() &&
           mach_absolute_time() >= state_.audio_end_tick.load(std::memory_order_acquire);
  }

private:
  static double ticks_per_second() {
    mach_timebase_info_data_t timebase{};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.numer == 0)
      throw std::runtime_error("Could not inspect the audio host clock.");
    return 1e9 * timebase.denom / timebase.numer;
  }
  void check_error() const {
    check(state_.error.load(std::memory_order_relaxed), "Audio output failed");
    state_.playback.rethrow_if_failed();
  }
  OutputState state_;
  UnitOwner owner_;
  bool started_ = false;
};
} // namespace

std::unique_ptr<PlaybackOutput> make_audio_output(BufferedPlayback &playback,
                                                 double sample_rate) {
  return std::make_unique<MacAudioOutput>(playback, sample_rate);
}

PlaybackReport play_song(PreparedSong song) {
  const auto rate = song.sample_rate;
  BufferedPlayback playback(std::move(song));
  playback.wait_until_ready();
  auto output = make_audio_output(playback, rate);
  output->play();
  auto last_frame = output->position_frames();
  auto last_activity = std::chrono::steady_clock::now();
  while (!output->finished()) {
    const auto frame = output->position_frames();
    if (frame != last_frame) {
      last_frame = frame;
      last_activity = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - last_activity > output_timeout) {
      throw std::runtime_error("Audio output stopped responding.");
    }
    std::this_thread::sleep_for(2ms);
  }
  return playback.report();
}

} // namespace playset
