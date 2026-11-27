#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace playset {

enum class VolumeSmoothing { exponential, linear };

using TrackId = std::size_t;
inline constexpr std::size_t maximum_tracks = 256;

class MixerControls {
public:
  explicit MixerControls(std::size_t count);
  // Publish from one control thread; batch updates are adopted together.
  void set(TrackId id, double volume);
  void set_all(std::span<const double> volumes);
  void set_muted(TrackId id, bool muted);
  void set_muted(std::span<const TrackId> ids, bool muted);
  void set_solo(TrackId id, bool enabled);
  void set_soli(std::span<const TrackId> ids, bool enabled);
  void set_master_volume(double volume);
  void set_master_muted(bool muted);
  [[nodiscard]] double master_volume() const noexcept;
  [[nodiscard]] bool master_muted() const noexcept;
  [[nodiscard]] double get(TrackId id) const;
  [[nodiscard]] bool is_muted(TrackId id) const;
  [[nodiscard]] bool is_solo(TrackId id) const;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
  friend class VolumeSmoother;
  void change_solo(std::span<const TrackId> ids, bool enabled, bool exclusive);
  std::size_t count_;
  // One control-thread publisher; odd revisions mark an unfinished update.
  std::atomic<std::uint64_t> revision_{0};
  std::array<std::atomic<double>, maximum_tracks> targets_{};
  std::array<std::atomic<bool>, maximum_tracks> muted_{};
  std::array<std::atomic<bool>, maximum_tracks> solo_{};
  std::atomic<double> master_volume_{1};
  std::atomic<bool> master_muted_{false};
};

// One audio consumer owns the smoothing state; the control thread only writes targets.
class VolumeSmoother {
public:
  VolumeSmoother(const MixerControls &volumes, double sample_rate,
                 VolumeSmoothing mode = VolumeSmoothing::exponential);
  void begin_render() noexcept;
  void settle() noexcept;
  std::span<const double> next_frame() noexcept;
  [[nodiscard]] double master_gain() const noexcept;

private:
  const MixerControls &volumes_;
  VolumeSmoothing mode_;
  double smoothing_;
  std::size_t ramp_frames_;
  bool initialized_ = false;
  std::uint64_t revision_ = 0;
  // The entry after the last track holds the combined output's multiplier.
  std::array<double, maximum_tracks + 1> pending_{};
  std::array<double, maximum_tracks + 1> current_{};
  std::array<double, maximum_tracks + 1> target_{};
  std::array<double, maximum_tracks + 1> step_{};
  std::array<std::size_t, maximum_tracks + 1> remaining_{};
};

} // namespace playset
