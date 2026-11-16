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
  std::atomic<std::uint64_t> callbacks{0};
};
static_assert(std::atomic<OSStatus>::is_always_lock_free);

void refill(void *user, AudioQueueRef queue,
            AudioQueueBufferRef buffer) noexcept {
  auto &state = *static_cast<OutputState *>(user);
  state.callbacks.fetch_add(1, std::memory_order_relaxed);
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
} // namespace

PlaybackReport play_song(PreparedSong song) {
  const auto sample_rate = song.sample_rate;
  BufferedPlayback playback(std::move(song));
  OutputState state{playback};
  QueueOwner owner{state};
  AudioStreamBasicDescription format{};
  format.mSampleRate = sample_rate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
  format.mChannelsPerFrame = output_channels;
  format.mBitsPerChannel = 32;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = output_channels * sizeof(float);
  format.mBytesPerPacket = format.mBytesPerFrame;
  check(AudioQueueNewOutput(&format, refill, &state, nullptr, nullptr, 0,
                            &owner.queue),
        "Could not create audio output");
  playback.wait_until_ready();
  for (std::size_t index = 0; index < playback_prefill_blocks; ++index) {
    AudioQueueBufferRef buffer = nullptr;
    check(AudioQueueAllocateBuffer(owner.queue, buffer_bytes, &buffer),
          "Could not allocate output buffer");
    refill(&state, owner.queue, buffer);
    check(state.error.load(std::memory_order_relaxed),
          "Could not prime audio output");
  }
  check(AudioQueueStart(owner.queue, nullptr), "Could not start audio output");
  auto last_callback = state.callbacks.load(std::memory_order_relaxed);
  auto last_activity = std::chrono::steady_clock::now();
  while (!playback.exhausted()) {
    check(state.error.load(std::memory_order_relaxed), "Audio output failed");
    const auto callbacks = state.callbacks.load(std::memory_order_relaxed);
    if (callbacks != last_callback) {
      last_callback = callbacks;
      last_activity = std::chrono::steady_clock::now();
    } else if (std::chrono::steady_clock::now() - last_activity > output_timeout) {
      throw std::runtime_error("Audio output stopped responding.");
    }
    std::this_thread::sleep_for(2ms);
  }
  check(state.error.load(std::memory_order_relaxed), "Audio output failed");
  playback.rethrow_if_failed();
  // Drain queued and hardware-buffered audio before disposing of the output.
  check(AudioQueueStop(owner.queue, false), "Could not finish audio output");
  const auto deadline = std::chrono::steady_clock::now() + output_timeout;
  while (true) {
    UInt32 running = 0;
    UInt32 size = sizeof(running);
    check(AudioQueueGetProperty(owner.queue, kAudioQueueProperty_IsRunning,
                                &running, &size),
          "Could not inspect audio output");
    if (!running)
      break;
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("Audio output did not finish.");
    std::this_thread::sleep_for(2ms);
  }
  return playback.report();
}

} // namespace playset
