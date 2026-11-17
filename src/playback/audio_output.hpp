#pragma once

#include "buffered_playback.hpp"

#include <memory>

namespace playset {

// Methods belong to one control thread. Destruction quiesces all callbacks.
class PlaybackOutput {
public:
  virtual ~PlaybackOutput() = default;
  virtual void play() = 0;
  virtual void pause() = 0;
  [[nodiscard]] virtual std::uint64_t position_frames() const = 0;
  [[nodiscard]] virtual bool finished() = 0;
};

[[nodiscard]] std::unique_ptr<PlaybackOutput>
make_audio_output(BufferedPlayback &playback, double sample_rate);

[[nodiscard]] PlaybackReport play_song(PreparedSong song);

} // namespace playset
