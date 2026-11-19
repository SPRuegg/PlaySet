#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <span>

namespace playset {

enum class VolumeSmoothing { exponential, linear };

using TrackId = std::size_t;
inline constexpr std::size_t maximum_tracks = 256;

class TrackVolumes {
public:
  explicit TrackVolumes(std::size_t count);
  void set(TrackId id, double volume);
  [[nodiscard]] double get(TrackId id) const;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
  friend class VolumeSmoother;
  std::size_t count_;
  std::array<std::atomic<double>, maximum_tracks> targets_{};
};

// One audio consumer owns the smoothing state; the control thread only writes targets.
class VolumeSmoother {
public:
  VolumeSmoother(const TrackVolumes &volumes, double sample_rate,
                 VolumeSmoothing mode = VolumeSmoothing::exponential);
  void begin_render() noexcept;
  void settle() noexcept;
  std::span<const double> next_frame() noexcept;

private:
  const TrackVolumes &volumes_;
  VolumeSmoothing mode_;
  double smoothing_;
  std::size_t ramp_frames_;
  bool initialized_ = false;
  std::array<double, maximum_tracks> current_{};
  std::array<double, maximum_tracks> target_{};
  std::array<double, maximum_tracks> step_{};
  std::array<std::size_t, maximum_tracks> remaining_{};
};

} // namespace playset
