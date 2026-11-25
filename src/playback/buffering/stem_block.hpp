#pragma once

#include "playback/loading/stem_loader.hpp"
#include "playback/mixing/track_volume.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace playset {

inline constexpr std::size_t playback_block_frames = 512;
inline constexpr std::size_t output_channels = 2;
inline constexpr std::size_t playback_prefill_blocks = 3;

class StemBlock {
public:
  explicit StemBlock(std::size_t track_count);
  [[nodiscard]] std::size_t track_count() const noexcept;
  std::span<float> track(std::size_t index) noexcept;
  std::span<const float> track(std::size_t index) const noexcept;
  std::size_t frames = 0;

private:
  std::vector<float> samples_;
};

std::size_t decode_stem_block(PreparedSong &song, StemBlock &block);

// The final output boundary: sum with double precision, count overloads, then clamp.
std::uint64_t mix_stem_block(const StemBlock &block, std::size_t first_frame,
                             std::span<float> output, VolumeSmoother *volume = nullptr) noexcept;

} // namespace playset
