#include "playback/audio_output.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace playset {
namespace {
using namespace std::chrono_literals;
constexpr UInt32 buffer_bytes =
    playback_block_frames * output_channels * sizeof(float);
constexpr auto output_timeout = 10s;

void check(OSStatus status, const char *operation) {
  if (status != noErr) {
    throw std::runtime_error(std::string(operation) + " (AudioQueue " +
                             std::to_string(status) + ")");
  }
}

struct OutputState {
  BufferedPlayback &playback;
  std::atomic<OSStatus> error{noErr};
  std::atomic<bool> closing{false};
};
static_assert(std::atomic<OSStatus>::is_always_lock_free);

void refill(void *user, AudioQueueRef queue,
            AudioQueueBufferRef buffer) noexcept {
  auto &state = *static_cast<OutputState *>(user);
  if (state.closing.load(std::memory_order_acquire) ||
      state.playback.exhausted() ||
      state.error.load(std::memory_order_relaxed) != noErr)
    return;
  auto output = std::span(static_cast<float *>(buffer->mAudioData),
                          playback_block_frames * output_channels);
  const auto frames = state.playback.render(output);
  if (state.playback.exhausted())
    return;
  // A temporary underrun outputs silence without advancing the source timeline.
  buffer->mAudioDataByteSize =
      frames == 0
          ? buffer_bytes
          : static_cast<UInt32>(frames * output_channels * sizeof(float));
  const auto status = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  if (status != noErr)
    state.error.store(status, std::memory_order_relaxed);
}

struct QueueOwner {
  OutputState &state;
  AudioQueueRef queue = nullptr;
  ~QueueOwner() {
    state.closing.store(true, std::memory_order_release);
    if (queue)
      AudioQueueDispose(queue, true);
  }
};
class MacAudioOutput final : public PlaybackOutput {
public:
  MacAudioOutput(BufferedPlayback &playback, double sample_rate)
      : state_{playback}, owner_{state_} {
    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    format.mChannelsPerFrame = output_channels;
    format.mBitsPerChannel = 32;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = output_channels * sizeof(float);
    format.mBytesPerPacket = format.mBytesPerFrame;
    check(AudioQueueNewOutput(&format, refill, &state_, nullptr, nullptr, 0,
                              &owner_.queue), "Could not create audio output");
    for (std::size_t index = 0; index < playback_prefill_blocks; ++index) {
      AudioQueueBufferRef buffer = nullptr;
      check(AudioQueueAllocateBuffer(owner_.queue, buffer_bytes, &buffer),
            "Could not allocate output buffer");
      refill(&state_, owner_.queue, buffer);
      check_error();
    }
  }

  void play() override {
    check_error();
    check(AudioQueueStart(owner_.queue, nullptr), "Could not start audio output");
    started_ = true;
    paused_ = false;
  }
  void pause() override {
    if (!started_ || paused_) return;
    check(AudioQueuePause(owner_.queue), "Could not pause audio output");
    paused_frame_ = current_frame();
    paused_ = true;
  }
  std::uint64_t position_frames() const override {
    if (!started_) return 0;
    return paused_ ? paused_frame_ : current_frame();
  }
  bool finished() override {
    check_error();
    if (!state_.playback.exhausted()) return false;
    if (!draining_) {
      check(AudioQueueStop(owner_.queue, false), "Could not finish audio output");
      draining_ = true;
    }
    UInt32 running = 0;
    UInt32 size = sizeof(running);
    check(AudioQueueGetProperty(owner_.queue, kAudioQueueProperty_IsRunning,
                                &running, &size), "Could not inspect audio output");
    return running == 0;
  }

private:
  void check_error() const {
    check(state_.error.load(std::memory_order_relaxed), "Audio output failed");
    state_.playback.rethrow_if_failed();
  }
  std::uint64_t current_frame() const {
    AudioTimeStamp time{};
    const auto status = AudioQueueGetCurrentTime(owner_.queue, nullptr, &time, nullptr);
    // The clock can be unavailable while the queue starts or finishes draining.
    if (status == kAudioQueueErr_InvalidRunState) return last_frame_;
    check(status, "Could not inspect playback position");
    if (!(time.mFlags & kAudioTimeStampSampleTimeValid) || time.mSampleTime < 0)
      return last_frame_;
    last_frame_ = static_cast<std::uint64_t>(time.mSampleTime);
    return last_frame_;
  }
  OutputState state_;
  QueueOwner owner_;
  bool started_ = false;
  bool paused_ = false;
  bool draining_ = false;
  std::uint64_t paused_frame_ = 0;
  mutable std::uint64_t last_frame_ = 0;
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
