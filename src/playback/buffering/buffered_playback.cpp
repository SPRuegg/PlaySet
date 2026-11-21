#include "buffered_playback.hpp"

#include <chrono>
#include <utility>
#include <stdexcept>

namespace playset {
namespace {
using namespace std::chrono_literals;
}

static_assert(std::atomic<bool>::is_always_lock_free);

BufferedPlayback::BufferedPlayback(PreparedSong song, std::int64_t start_frame,
                                   std::shared_ptr<TrackVolumes> volumes,
                                   VolumeSmoothing smoothing)
    : song_(std::move(song)), queue_(song_.stems.size()), volumes_(std::move(volumes)) {
  if (volumes_) {
    if (volumes_->size() != song_.stems.size())
      throw std::invalid_argument("Volume controls must match the song tracks.");
    volume_smoother_ = std::make_unique<VolumeSmoother>(*volumes_, song_.sample_rate, smoothing);
  }
  for (auto &stem : song_.stems)
    stem.seek(start_frame);
  producer_ = std::jthread([this](std::stop_token stop) { produce(stop); });
}

BufferedPlayback::~BufferedPlayback() {
  producer_.request_stop();
  // Join before any queue, error, or song state can be destroyed.
  producer_.join();
}

void BufferedPlayback::produce(std::stop_token stop) noexcept {
  try {
    while (!stop.stop_requested()) {
      auto *block = queue_.begin_write();
      if (!block) {
        std::this_thread::sleep_for(1ms);
        continue;
      }
      if (decode_stem_block(song_, *block) == 0)
        break;
      queue_.commit_write();
    }
  } catch (...) {
    error_ = std::current_exception();
  }
  producer_done_.store(true, std::memory_order_release);
}

void BufferedPlayback::wait_until_ready() {
  while (!producer_done_.load(std::memory_order_acquire) &&
         queue_.ready_blocks() < playback_prefill_blocks) {
    std::this_thread::sleep_for(1ms);
  }
  rethrow_if_failed();
}

std::size_t BufferedPlayback::render(std::span<float> output) noexcept {
  if (output.empty()) return 0;
  auto producer_finished = producer_done_.load(std::memory_order_acquire);
  if (volume_smoother_) volume_smoother_->begin_render();
  auto result = queue_.render(output, volume_smoother_.get());
  if (result.frames < output.size() / output_channels && !producer_finished &&
      producer_done_.load(std::memory_order_acquire)) {
    // Completion may publish after the first read; drain any final block before
    // EOF.
    const auto remaining =
        queue_.render(output.subspan(result.frames * output_channels), volume_smoother_.get());
    result.frames += remaining.frames;
    result.clipped_samples += remaining.clipped_samples;
    producer_finished = true;
  }
  rendered_frames_.fetch_add(result.frames, std::memory_order_relaxed);
  clipped_samples_.fetch_add(result.clipped_samples, std::memory_order_relaxed);
  if (result.frames == 0 && producer_finished)
    exhausted_.store(true, std::memory_order_release);
  if (result.frames < output.size() / output_channels && !producer_finished) {
    underruns_.fetch_add(1, std::memory_order_relaxed);
  }
  return result.frames;
}

void BufferedPlayback::settle_volumes() noexcept {
  if (volume_smoother_) volume_smoother_->settle();
}

bool BufferedPlayback::exhausted() const noexcept {
  return exhausted_.load(std::memory_order_acquire);
}

PlaybackReport BufferedPlayback::report() const noexcept {
  return {rendered_frames_.load(std::memory_order_relaxed),
          clipped_samples_.load(std::memory_order_relaxed),
          underruns_.load(std::memory_order_relaxed)};
}

void BufferedPlayback::rethrow_if_failed() const {
  if (producer_done_.load(std::memory_order_acquire) && error_)
    std::rethrow_exception(error_);
}

} // namespace playset
