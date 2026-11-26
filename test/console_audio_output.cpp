#include "playback/output/audio_output.hpp"

#include <stdexcept>

namespace playset {
namespace {
// This test driver runs the real console without connecting an audio device.
class SilentOutput final : public PlaybackOutput {
public:
  explicit SilentOutput(BufferedPlayback &playback) : playback_(playback) {}
  void play() override { playback_.settle_volumes(); }
  void pause() override {}
  void seek(std::int64_t frame) override {
    playback_.prepare_seek(frame);
    const auto origin = playback_.apply_pending_seek();
    if (!origin) throw std::runtime_error("Seek bank missing.");
    frame_ = *origin;
    playback_.acknowledge_seek();
  }
  std::int64_t position_frames() const override { return frame_; }
  bool finished() override { return false; }
private:
  BufferedPlayback &playback_;
  std::int64_t frame_ = 0;
};
}
std::unique_ptr<PlaybackOutput> make_audio_output(BufferedPlayback &playback, double) {
  return std::make_unique<SilentOutput>(playback);
}
PlaybackReport play_song(PreparedSong) {
  throw std::runtime_error("The console test driver does not play audio.");
}
}
