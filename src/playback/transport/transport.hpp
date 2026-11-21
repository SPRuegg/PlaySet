#pragma once

#include "playback/output/audio_output.hpp"

#include <functional>
#include <string>

namespace playset {

enum class TransportStatus { stopped, playing, paused, ended };
struct TrackSnapshot {
  TrackId id;
  std::string name;
  double volume;
};

struct TransportSnapshot {
  TransportStatus status;
  double position_seconds;
  double duration_seconds;
};

// All methods belong to one control thread; audio rendering remains independent.
class Transport {
public:
  using OutputFactory = std::function<std::unique_ptr<PlaybackOutput>(
      BufferedPlayback &, double)>;
  explicit Transport(PreparedSong song, OutputFactory output = make_audio_output,
                     VolumeSmoothing smoothing = VolumeSmoothing::exponential);
  ~Transport();
  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;

  void play();
  void pause();
  void stop();
  void seek(double seconds);
  void set_track_volume(TrackId id, double volume);
  [[nodiscard]] std::vector<TrackSnapshot> tracks() const;
  [[nodiscard]] TransportSnapshot snapshot();

private:
  struct Session;
  void replace(std::int64_t frame, TransportStatus status);
  std::vector<StemInfo> stems_;
  double sample_rate_;
  std::int64_t total_frames_ = 0;
  OutputFactory output_factory_;
  VolumeSmoothing smoothing_;
  std::shared_ptr<TrackVolumes> volumes_;
  std::unique_ptr<Session> session_;
  TransportStatus status_ = TransportStatus::stopped;
  bool rewind_required_ = false;
};

} // namespace playset
