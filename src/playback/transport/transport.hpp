#pragma once

#include "playback/output/audio_output.hpp"

#include <functional>
#include <span>
#include <string>

namespace playset {

enum class TransportStatus { stopped, playing, paused, ended };
struct TrackSnapshot {
  TrackId id;
  std::string name;
  double volume;
  bool muted = false;
  bool solo = false;
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
  void set_track_muted(TrackId id, bool muted);
  void set_tracks_muted(std::span<const TrackId> ids, bool muted);
  void set_track_solo(TrackId id, bool enabled);
  void set_tracks_soli(std::span<const TrackId> ids, bool enabled);
  [[nodiscard]] std::vector<TrackSnapshot> tracks() const;
  [[nodiscard]] TransportSnapshot snapshot();

private:
  std::vector<StemInfo> stems_;
  double sample_rate_;
  std::int64_t total_frames_ = 0;
  std::shared_ptr<MixerControls> volumes_;
  std::unique_ptr<BufferedPlayback> playback_;
  std::unique_ptr<PlaybackOutput> output_;
  TransportStatus status_ = TransportStatus::stopped;
};

} // namespace playset
