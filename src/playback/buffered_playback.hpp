#pragma once

#include "stem_block_queue.hpp"

#include <atomic>
#include <exception>
#include <thread>

namespace playset {

struct PlaybackReport {
  std::uint64_t rendered_frames = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t underruns = 0;
};

class BufferedPlayback {
public:
  explicit BufferedPlayback(PreparedSong song);
  ~BufferedPlayback();
  // Control thread only, before the output consumer starts.
  void wait_until_ready();
  // One output consumer only; never decodes, allocates, or waits.
  std::size_t render(std::span<float> output) noexcept;
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] PlaybackReport report() const noexcept;
  void rethrow_if_failed() const;

private:
  void produce(std::stop_token stop) noexcept;
  PreparedSong song_;
  StemBlockQueue queue_;
  std::exception_ptr error_;
  std::atomic<bool> producer_done_{false};
  std::atomic<bool> exhausted_{false};
  std::atomic<std::uint64_t> rendered_frames_{0};
  std::atomic<std::uint64_t> clipped_samples_{0};
  std::atomic<std::uint64_t> underruns_{0};
  std::jthread producer_;
};

} // namespace playset
