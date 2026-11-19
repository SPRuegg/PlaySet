#include "stem_block.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace playset {
static_assert(playback_block_frames <= decode_block_frames);
namespace {
constexpr auto samples_per_track = playback_block_frames * output_channels;
}

StemBlock::StemBlock(std::size_t track_count) {
  if (track_count == 0 || track_count > maximum_tracks) {
    throw std::invalid_argument(
        "A playback block requires between 1 and 256 tracks.");
  }
  samples_.resize(track_count * samples_per_track);
}

std::size_t StemBlock::track_count() const noexcept {
  return samples_.size() / samples_per_track;
}

std::span<float> StemBlock::track(std::size_t index) noexcept {
  assert(index < track_count());
  return std::span(samples_).subspan(index * samples_per_track,
                                     samples_per_track);
}

std::span<const float> StemBlock::track(std::size_t index) const noexcept {
  assert(index < track_count());
  return std::span(samples_).subspan(index * samples_per_track,
                                     samples_per_track);
}

std::size_t decode_stem_block(PreparedSong &song, StemBlock &block) {
  if (song.stems.size() != block.track_count()) {
    throw std::invalid_argument(
        "Playback block must match the song's track count.");
  }
  block.frames = 0;
  for (std::size_t index = 0; index < song.stems.size(); ++index) {
    block.frames =
        std::max(block.frames, song.stems[index].read(block.track(index)));
  }
  return block.frames;
}

std::uint64_t mix_stem_block(const StemBlock &block, std::size_t first_frame,
                             std::span<float> output, VolumeSmoother *volume) noexcept {
  assert(output.size() % output_channels == 0);
  assert(first_frame <= block.frames);
  assert(output.size() / output_channels <= block.frames - first_frame);
  std::uint64_t clipped = 0;
  for (std::size_t frame = 0; frame < output.size() / output_channels;
       ++frame) {
    const auto gains = volume ? volume->next_frame() : std::span<const double>{};
    assert(!volume || gains.size() == block.track_count());
    for (std::size_t channel = 0; channel < output_channels; ++channel) {
      const auto source_sample = (first_frame + frame) * output_channels + channel;
      double sum = 0;
      for (std::size_t track = 0; track < block.track_count(); ++track) {
        sum += block.track(track)[source_sample] * (volume ? gains[track] : 1.0);
      }
      if (sum < -1.0 || sum > 1.0)
        ++clipped;
      output[frame * output_channels + channel] =
          static_cast<float>(std::clamp(sum, -1.0, 1.0));
    }
  }
  return clipped;
}

} // namespace playset
