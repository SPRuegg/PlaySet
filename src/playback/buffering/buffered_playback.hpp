#pragma once

#include "stem_block_queue.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>

namespace playset {

struct PlaybackReport {
  std::uint64_t rendered_frames = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t underruns = 0;
};

class BufferedPlayback {
public:
  explicit BufferedPlayback(PreparedSong song, std::int64_t start_frame = 0,
                            std::shared_ptr<TrackVolumes> volumes = nullptr,
                            VolumeSmoothing smoothing = VolumeSmoothing::exponential);
  ~BufferedPlayback();
  // Control thread only, before the output consumer starts.
  void wait_until_ready();
  // One output consumer only; never decodes, allocates, or waits.
  std::size_t render(std::span<float> output) noexcept;
  // Audio consumer only, while silent or immediately before resuming.
  void settle_volumes() noexcept;
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] PlaybackReport report() const noexcept;
  void rethrow_if_failed() const;

private:
  void produce(std::stop_token stop) noexcept;
  PreparedSong song_;
  StemBlockQueue queue_;
  std::shared_ptr<TrackVolumes> volumes_;
  std::unique_ptr<VolumeSmoother> volume_smoother_;
  std::exception_ptr error_;
  bool stalled_ = false;
  std::atomic<bool> producer_done_{false};
  std::atomic<bool> exhausted_{false};
  std::atomic<std::uint64_t> rendered_frames_{0};
  std::atomic<std::uint64_t> clipped_samples_{0};
  std::atomic<std::uint64_t> underruns_{0};
  std::jthread producer_;
};

} // namespace playset
