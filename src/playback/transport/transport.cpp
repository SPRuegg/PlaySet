#include "transport.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace playset {

Transport::Transport(PreparedSong song, OutputFactory output, VolumeSmoothing smoothing)
    : sample_rate_(song.sample_rate) {
  for (const auto &stem : song.stems) {
    stems_.push_back(stem.info());
    total_frames_ = std::max(total_frames_, stem.info().frames);
  }
  if (stems_.empty() || !std::isfinite(sample_rate_) || sample_rate_ <= 0)
    throw std::invalid_argument("Transport requires a prepared song.");
  volumes_ = std::make_shared<TrackVolumes>(stems_.size());
  playback_ = std::make_unique<BufferedPlayback>(std::move(song), 0, volumes_, smoothing);
  playback_->wait_until_ready();
  output_ = output(*playback_, sample_rate_);
}
Transport::~Transport() = default;

TransportSnapshot Transport::snapshot() {
  playback_->rethrow_if_failed();
  if (status_ == TransportStatus::playing && output_->finished())
    status_ = TransportStatus::ended;
  const auto frame = status_ == TransportStatus::ended ? total_frames_ :
      std::min(total_frames_, output_->position_frames());
  return {status_, static_cast<double>(frame) / sample_rate_,
          static_cast<double>(total_frames_) / sample_rate_};
}

void Transport::play() {
  const auto state = snapshot();
  if (state.status == TransportStatus::playing) return;
  if (state.status == TransportStatus::ended || state.position_seconds == state.duration_seconds)
    output_->seek(0);
  output_->play();
  status_ = TransportStatus::playing;
}
void Transport::pause() {
  if (snapshot().status != TransportStatus::playing) return;
  output_->pause();
  status_ = TransportStatus::paused;
}
void Transport::stop() {
  output_->pause();
  status_ = TransportStatus::paused;
  output_->seek(0);
  status_ = TransportStatus::stopped;
}
void Transport::seek(double seconds) {
  const auto duration = static_cast<double>(total_frames_) / sample_rate_;
  if (!std::isfinite(seconds) || seconds < 0 || seconds > duration)
    throw std::invalid_argument("Seek position must be within the song duration.");
  const auto requested_frame = seconds * sample_rate_;
  const auto frame = requested_frame >= static_cast<double>(total_frames_)
                         ? total_frames_
                         : static_cast<std::int64_t>(std::llround(requested_frame));
  auto status = snapshot().status;
  if (status == TransportStatus::ended) {
    output_->pause();
    status = TransportStatus::paused;
  }
  output_->seek(frame);
  status_ = frame == total_frames_ && status == TransportStatus::playing
                ? TransportStatus::ended : status;
}

void Transport::set_track_volume(TrackId id, double volume) {
  volumes_->set(id, volume);
}

std::vector<TrackSnapshot> Transport::tracks() const {
  std::vector<TrackSnapshot> result;
  result.reserve(stems_.size());
  for (TrackId id = 0; id < stems_.size(); ++id)
    result.push_back({id, stems_[id].path.stem().string(), volumes_->get(id)});
  return result;
}

} // namespace playset
