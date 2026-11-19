#include "track_volume.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace playset {
static_assert(std::atomic<double>::is_always_lock_free);

TrackVolumes::TrackVolumes(std::size_t count) : count_(count) {
  if (count == 0 || count > maximum_tracks)
    throw std::invalid_argument("Volume controls require between 1 and 256 tracks.");
  for (std::size_t id = 0; id < count; ++id)
    targets_[id].store(1, std::memory_order_relaxed);
}
void TrackVolumes::set(TrackId id, double volume) {
  if (id >= count_) throw std::out_of_range("Unknown track ID.");
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::invalid_argument("Track volume must be between 0 and 1.");
  targets_[id].store(volume, std::memory_order_relaxed);
}
double TrackVolumes::get(TrackId id) const {
  if (id >= count_) throw std::out_of_range("Unknown track ID.");
  return targets_[id].load(std::memory_order_relaxed);
}
VolumeSmoother::VolumeSmoother(const TrackVolumes &volumes, double sample_rate,
                               VolumeSmoothing mode)
    : volumes_(volumes), mode_(mode) {
  if (!std::isfinite(sample_rate) || sample_rate < 8000 || sample_rate > 192000)
    throw std::invalid_argument("Invalid volume ramp sample rate.");
  if (mode != VolumeSmoothing::exponential && mode != VolumeSmoothing::linear)
    throw std::invalid_argument("Unknown volume smoothing mode.");
  constexpr double smoothing_seconds = 0.005;
  smoothing_ = 1 - std::exp(-1 / (sample_rate * smoothing_seconds));
  ramp_frames_ = std::max<std::size_t>(1, std::llround(sample_rate * smoothing_seconds));
}
void VolumeSmoother::begin_render() noexcept {
  for (TrackId id = 0; id < volumes_.size(); ++id) {
    const auto value = volumes_.targets_[id].load(std::memory_order_relaxed);
    if (!initialized_) {
      current_[id] = target_[id] = value;
    } else if (value != target_[id]) {
      target_[id] = value;
      if (mode_ == VolumeSmoothing::linear) {
        remaining_[id] = ramp_frames_;
        step_[id] = (value - current_[id]) / static_cast<double>(ramp_frames_);
      }
    }
  }
  initialized_ = true;
}
void VolumeSmoother::settle() noexcept {
  begin_render();
  for (TrackId id = 0; id < volumes_.size(); ++id) {
    current_[id] = target_[id];
    remaining_[id] = 0;
  }
}
std::span<const double> VolumeSmoother::next_frame() noexcept {
  for (TrackId id = 0; id < volumes_.size(); ++id) {
    if (mode_ == VolumeSmoothing::exponential) {
      current_[id] += (target_[id] - current_[id]) * smoothing_;
      // Settle below -180 dB amplitude error to reach exact endpoints and silence.
      if (std::abs(target_[id] - current_[id]) <= 1e-9) current_[id] = target_[id];
    } else if (remaining_[id] > 0) {
      current_[id] += step_[id];
      if (--remaining_[id] == 0) current_[id] = target_[id];
    }
  }
  return std::span(current_).first(volumes_.size());
}

} // namespace playset
