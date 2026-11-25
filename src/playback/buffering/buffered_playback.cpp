#include "buffered_playback.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <stdexcept>

namespace playset {
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);

BufferedPlayback::BufferedPlayback(PreparedSong song, std::int64_t start_frame,
                                   std::shared_ptr<TrackVolumes> volumes,
                                   VolumeSmoothing smoothing)
    : song_(std::move(song)), volumes_(std::move(volumes)) {
  for (auto &bank : banks_) bank = std::make_unique<Bank>(song_.stems.size());
  banks_[0]->origin = start_frame;
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
  seek_request_.fetch_add(1, std::memory_order_release);
  seek_request_.notify_one();
  // Join before any queue, error, or song state can be destroyed.
  producer_.join();
}

void BufferedPlayback::produce(std::stop_token stop) noexcept {
  try {
    unsigned bank_index = 0;
    std::uint64_t request = 0;
    bool preparing = false;
    while (!stop.stop_requested()) {
      const auto latest = seek_request_.load(std::memory_order_acquire);
      if (latest != request) {
        request = latest;
        bank_index = 1 - active_bank_.load(std::memory_order_acquire);
        auto &bank = *banks_[bank_index];
        bank.queue.reset();
        bank.finished.store(false, std::memory_order_relaxed);
        bank.origin = seek_frame_.load(std::memory_order_relaxed);
        for (auto &stem : song_.stems) stem.seek(bank.origin);
        preparing = true;
      }
      auto &bank = *banks_[bank_index];
      if (preparing && (bank.finished.load(std::memory_order_relaxed) ||
                        bank.queue.ready_blocks() >= playback_prefill_blocks)) {
        seek_prepared_.store(request, std::memory_order_release);
        while (!stop.stop_requested() &&
               seek_applied_.load(std::memory_order_acquire) != request)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        preparing = false;
        continue;
      }
      if (bank.finished.load(std::memory_order_relaxed)) {
        seek_request_.wait(request, std::memory_order_acquire);
        continue;
      }
      auto *block = bank.queue.begin_write();
      if (!block) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (decode_stem_block(song_, *block) == 0)
        bank.finished.store(true, std::memory_order_release);
      else bank.queue.commit_write();
    }
  } catch (...) {
    error_ = std::current_exception();
    for (auto &bank : banks_) bank->finished.store(true, std::memory_order_release);
    failed_.store(true, std::memory_order_release);
  }
}

void BufferedPlayback::wait_until_ready() {
  while (!failed_.load(std::memory_order_acquire) &&
         !banks_[0]->finished.load(std::memory_order_acquire) &&
         banks_[0]->queue.ready_blocks() < playback_prefill_blocks)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rethrow_if_failed();
}

void BufferedPlayback::prepare_seek(std::int64_t frame) {
  rethrow_if_failed();
  std::int64_t length = 0;
  for (const auto &stem : song_.stems) length = std::max(length, stem.info().frames);
  if (frame < 0 || frame > length)
    throw std::invalid_argument("Seek frame must be within the song duration.");
  if (seek_pending()) throw std::logic_error("A seek is already pending.");
  seek_frame_.store(frame, std::memory_order_relaxed);
  const auto request = seek_request_.fetch_add(1, std::memory_order_release) + 1;
  seek_request_.notify_one();
  while (!failed_.load(std::memory_order_acquire) &&
         seek_prepared_.load(std::memory_order_acquire) != request)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rethrow_if_failed();
}

bool BufferedPlayback::seek_pending() const noexcept {
  return seek_request_.load(std::memory_order_acquire) !=
         seek_applied_.load(std::memory_order_acquire);
}

std::optional<std::int64_t> BufferedPlayback::apply_pending_seek() noexcept {
  const auto prepared = seek_prepared_.load(std::memory_order_acquire);
  if (prepared == seek_applied_.load(std::memory_order_relaxed)) return std::nullopt;
  const auto next = 1 - active_bank_.load(std::memory_order_relaxed);
  const auto origin = banks_[next]->origin;
  active_bank_.store(next, std::memory_order_release);
  exhausted_.store(false, std::memory_order_release);
  stalled_ = false;
  return origin;
}

void BufferedPlayback::acknowledge_seek() noexcept {
  seek_applied_.store(seek_prepared_.load(std::memory_order_relaxed),
                      std::memory_order_release);
}

std::size_t BufferedPlayback::render(std::span<float> output) noexcept {
  if (output.empty()) return 0;
  auto &bank = *banks_[active_bank_.load(std::memory_order_relaxed)];
  const auto producer_finished = bank.finished.load(std::memory_order_acquire);
  if (volume_smoother_) volume_smoother_->begin_render();
  const auto result = bank.queue.render(output, volume_smoother_.get(), producer_finished);
  rendered_frames_.fetch_add(result.frames, std::memory_order_relaxed);
  clipped_samples_.fetch_add(result.clipped_samples, std::memory_order_relaxed);
  if (result.frames == 0 && producer_finished)
    exhausted_.store(true, std::memory_order_release);
  if (result.frames < output.size() / output_channels && !producer_finished) {
    if (!stalled_) underruns_.fetch_add(1, std::memory_order_relaxed);
    stalled_ = true;
  } else stalled_ = false;
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
  if (failed_.load(std::memory_order_acquire))
    std::rethrow_exception(error_);
}

} // namespace playset
