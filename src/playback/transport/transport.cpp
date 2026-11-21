#include "transport.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace playset {

struct Transport::Session {
  std::int64_t origin;
  BufferedPlayback playback;
  std::unique_ptr<PlaybackOutput> output;

  Session(PreparedSong song, std::int64_t frame, double rate, const OutputFactory &factory,
          std::shared_ptr<TrackVolumes> volumes, VolumeSmoothing smoothing)
      : origin(frame), playback(std::move(song), frame, std::move(volumes), smoothing) {
    playback.wait_until_ready();
    output = factory(playback, rate);
  }
};

Transport::Transport(PreparedSong song, OutputFactory output, VolumeSmoothing smoothing)
    : sample_rate_(song.sample_rate), output_factory_(std::move(output)), smoothing_(smoothing) {
  for (const auto &stem : song.stems) {
    stems_.push_back(stem.info());
    total_frames_ = std::max(total_frames_, stem.info().frames);
  }
  if (stems_.empty() || !std::isfinite(sample_rate_) || sample_rate_ <= 0)
    throw std::invalid_argument("Transport requires a prepared song.");
  volumes_ = std::make_shared<TrackVolumes>(stems_.size());
  session_ = std::make_unique<Session>(std::move(song), 0, sample_rate_, output_factory_, volumes_, smoothing_);
}
Transport::~Transport() = default;

void Transport::replace(std::int64_t frame, TransportStatus status) {
  PreparedSong song{sample_rate_, static_cast<double>(total_frames_) / sample_rate_, {}};
  for (const auto &info : stems_) {
    auto stem = StemReader::open(info.path);
    if (stem.info().sample_rate != sample_rate_ || stem.info().frames != info.frames ||
        stem.info().channels != info.channels || stem.info().format != info.format)
      throw std::runtime_error("Stem format or length changed after loading.");
    song.stems.push_back(std::move(stem));
  }
  auto replacement = std::make_unique<Session>(std::move(song), frame, sample_rate_, output_factory_, volumes_, smoothing_);
  session_->output->pause();
  session_.swap(replacement);
  status_ = frame == total_frames_ && status == TransportStatus::playing
                ? TransportStatus::ended : status;
  rewind_required_ = false;
  if (status_ == TransportStatus::playing)
    session_->output->play();
}

TransportSnapshot Transport::snapshot() {
  session_->playback.rethrow_if_failed();
  if (status_ == TransportStatus::playing && session_->output->finished())
    status_ = TransportStatus::ended;
  const auto frame = rewind_required_ ? 0 : status_ == TransportStatus::ended ? total_frames_ :
      std::min(total_frames_, session_->origin + session_->output->position_frames());
  return {status_, static_cast<double>(frame) / sample_rate_,
          static_cast<double>(total_frames_) / sample_rate_};
}

void Transport::play() {
  const auto state = snapshot();
  if (state.status == TransportStatus::playing) return;
  if (rewind_required_ || state.status == TransportStatus::ended || state.position_seconds == state.duration_seconds)
    replace(0, TransportStatus::stopped);
  session_->output->play();
  status_ = TransportStatus::playing;
}
void Transport::pause() {
  if (snapshot().status != TransportStatus::playing) return;
  session_->output->pause();
  status_ = TransportStatus::paused;
}
void Transport::stop() {
  if (status_ == TransportStatus::playing) session_->output->pause();
  status_ = TransportStatus::stopped;
  // Re-prime on the next Play/Seek; Stop itself never waits for disk access.
  rewind_required_ = true;
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
  if (status == TransportStatus::ended) status = TransportStatus::paused;
  replace(frame, status);
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
