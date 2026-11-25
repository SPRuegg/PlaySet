#pragma once

#include "stem_block_queue.hpp"

#include <array>
#include <atomic>
#include <exception>
#include <memory>
#include <optional>
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
  // One control thread prepares a seek; the audio consumer applies it at a boundary.
  void prepare_seek(std::int64_t frame);
  [[nodiscard]] bool seek_pending() const noexcept;
  std::optional<std::int64_t> apply_pending_seek() noexcept;
  // Audio consumer: publish only after updating output position and timing.
  void acknowledge_seek() noexcept;
  // One output consumer only; never decodes, allocates, or waits.
  std::size_t render(std::span<float> output) noexcept;
  // Audio consumer only, while silent or immediately before resuming.
  void settle_volumes() noexcept;
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] PlaybackReport report() const noexcept;
  void rethrow_if_failed() const;

private:
  struct Bank {
    explicit Bank(std::size_t tracks) : queue(tracks) {}
    StemBlockQueue queue;
    std::atomic<bool> finished{false};
    std::int64_t origin = 0;
  };
  void produce(std::stop_token stop) noexcept;
  PreparedSong song_;
  std::array<std::unique_ptr<Bank>, 2> banks_;
  std::shared_ptr<TrackVolumes> volumes_;
  std::unique_ptr<VolumeSmoother> volume_smoother_;
  std::exception_ptr error_;
  bool stalled_ = false;
  std::atomic<unsigned> active_bank_{0};
  std::atomic<std::int64_t> seek_frame_{0};
  std::atomic<std::uint64_t> seek_request_{0};
  std::atomic<std::uint64_t> seek_prepared_{0};
  std::atomic<std::uint64_t> seek_applied_{0};
  std::atomic<bool> failed_{false};
  std::atomic<bool> exhausted_{false};
  std::atomic<std::uint64_t> rendered_frames_{0};
  std::atomic<std::uint64_t> clipped_samples_{0};
  std::atomic<std::uint64_t> underruns_{0};
  std::jthread producer_;
};

} // namespace playset
