#pragma once

#include "audio_output.hpp"

#include <functional>

namespace playset {

enum class TransportStatus { stopped, playing, paused, ended };
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
  explicit Transport(PreparedSong song, OutputFactory output = make_audio_output);
  ~Transport();
  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;

  void play();
  void pause();
  void stop();
  void seek(double seconds);
  [[nodiscard]] TransportSnapshot snapshot();

private:
  struct Session;
  void replace(std::uint64_t frame, TransportStatus status);
  std::vector<StemInfo> stems_;
  double sample_rate_;
  std::uint64_t total_frames_ = 0;
  OutputFactory output_factory_;
  std::unique_ptr<Session> session_;
  TransportStatus status_ = TransportStatus::stopped;
  bool rewind_required_ = false;
};

} // namespace playset
