#include "track_volume.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace playset {
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

namespace {
void require_volume(double volume) {
  if (!std::isfinite(volume) || volume < 0 || volume > 1)
    throw std::invalid_argument("Track volume must be between 0 and 1.");
}
}

TrackVolumes::TrackVolumes(std::size_t count) : count_(count) {
  if (count == 0 || count > maximum_tracks)
    throw std::invalid_argument("Volume controls require between 1 and 256 tracks.");
  for (std::size_t id = 0; id < count; ++id)
    targets_[id].store(1, std::memory_order_relaxed);
}
void TrackVolumes::set(TrackId id, double volume) {
  if (id >= count_) throw std::out_of_range("Unknown track ID.");
  require_volume(volume);
  revision_.fetch_add(1);
  targets_[id].store(volume);
  revision_.fetch_add(1);
}
void TrackVolumes::set_all(std::span<const double> volumes) {
  if (volumes.size() != count_)
    throw std::invalid_argument("Volume batch must match the track count.");
  for (const auto volume : volumes) require_volume(volume);
  revision_.fetch_add(1);
  for (TrackId id = 0; id < count_; ++id) targets_[id].store(volumes[id]);
  revision_.fetch_add(1);
}
double TrackVolumes::get(TrackId id) const {
  if (id >= count_) throw std::out_of_range("Unknown track ID.");
  return targets_[id].load();
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
  std::fill_n(current_.begin(), volumes.size(), 1.0);
  std::fill_n(target_.begin(), volumes.size(), 1.0);
}
void VolumeSmoother::begin_render() noexcept {
  // Sequentially consistent revision and target accesses make this one snapshot.
  const auto revision = volumes_.revision_.load();
  if ((revision & 1) || (initialized_ && revision == revision_)) return;
  for (TrackId id = 0; id < volumes_.size(); ++id)
    pending_[id] = volumes_.targets_[id].load();
  // Reject a torn snapshot without waiting or changing any smoothing state.
  if (revision != volumes_.revision_.load()) return;
  for (TrackId id = 0; id < volumes_.size(); ++id) {
    const auto value = pending_[id];
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
  revision_ = revision;
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
