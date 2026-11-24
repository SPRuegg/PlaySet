#pragma once

#include "stem_block.hpp"

#include <atomic>

namespace playset {

struct RenderResult {
  std::size_t frames = 0;
  std::uint64_t clipped_samples = 0;
};

// One decoder producer and one output consumer. All storage is allocated up
// front.
class StemBlockQueue {
public:
  static constexpr std::size_t maximum_render_frames = 4096;
  // One extra block lets a full callback fit even with a partly consumed head.
  static constexpr std::size_t capacity = maximum_render_frames / playback_block_frames + 1;
  explicit StemBlockQueue(std::size_t track_count);
  StemBlock *begin_write() noexcept;
  void commit_write() noexcept;
  // At most maximum_render_frames; keep audio untouched on shortage unless finished.
  RenderResult render(std::span<float> output, VolumeSmoother *volume = nullptr,
                      bool producer_finished = false) noexcept;
  [[nodiscard]] std::size_t ready_blocks() const noexcept;

private:
  std::vector<StemBlock> blocks_;
  alignas(64) std::atomic<std::uint64_t> write_index_{0};
  alignas(64) std::atomic<std::uint64_t> read_index_{0};
  std::size_t read_offset_ = 0;
};

} // namespace playset
