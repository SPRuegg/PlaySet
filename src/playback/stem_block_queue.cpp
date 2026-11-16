#include "stem_block_queue.hpp"

#include <algorithm>
#include <cassert>

namespace playset {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(playback_prefill_blocks <= StemBlockQueue::capacity);

StemBlockQueue::StemBlockQueue(std::size_t track_count) {
  blocks_.reserve(capacity);
  for (std::size_t index = 0; index < capacity; ++index)
    blocks_.emplace_back(track_count);
}

StemBlock *StemBlockQueue::begin_write() noexcept {
  const auto write = write_index_.load(std::memory_order_relaxed);
  if (write - read_index_.load(std::memory_order_acquire) == capacity)
    return nullptr;
  return &blocks_[write % capacity];
}

void StemBlockQueue::commit_write() noexcept {
  const auto write = write_index_.load(std::memory_order_relaxed);
  assert(blocks_[write % capacity].frames > 0);
  assert(blocks_[write % capacity].frames <= playback_block_frames);
  write_index_.store(write + 1, std::memory_order_release);
}

RenderResult StemBlockQueue::render(std::span<float> output) noexcept {
  assert(output.size() % output_channels == 0);
  std::fill(output.begin(), output.end(), 0.0F);
  RenderResult result;
  auto read = read_index_.load(std::memory_order_relaxed);
  const auto requested = output.size() / output_channels;
  while (result.frames < requested &&
         read != write_index_.load(std::memory_order_acquire)) {
    const auto &block = blocks_[read % capacity];
    const auto count =
        std::min(requested - result.frames, block.frames - read_offset_);
    result.clipped_samples +=
        mix_stem_block(block, read_offset_,
                       output.subspan(result.frames * output_channels,
                                      count * output_channels));
    read_offset_ += count;
    result.frames += count;
    if (read_offset_ == block.frames) {
      read_offset_ = 0;
      read_index_.store(++read, std::memory_order_release);
    }
  }
  return result;
}

std::size_t StemBlockQueue::ready_blocks() const noexcept {
  // Only called by the consumer (or the control thread before output starts).
  const auto read = read_index_.load(std::memory_order_relaxed);
  return static_cast<std::size_t>(write_index_.load(std::memory_order_acquire) -
                                  read);
}

} // namespace playset
