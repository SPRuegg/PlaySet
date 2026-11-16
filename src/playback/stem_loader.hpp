#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace playset {

inline constexpr std::size_t decode_block_frames = 4096;

enum class StemFormat { wav, m4a };

struct StemInfo {
  std::filesystem::path path;
  StemFormat format;
  double sample_rate;
  std::uint32_t channels;
  std::uint64_t frames;

  [[nodiscard]] double duration_seconds() const noexcept;
};

// Blocking, single-thread-owned decoder. Never call from an audio callback.
// Output is interleaved stereo float PCM at the stem's original sample rate.
class StemReader {
public:
  static StemReader open(const std::filesystem::path &path);
  ~StemReader();
  StemReader(StemReader &&) noexcept;
  StemReader &operator=(StemReader &&) noexcept;
  StemReader(const StemReader &) = delete;
  StemReader &operator=(const StemReader &) = delete;

  [[nodiscard]] const StemInfo &info() const noexcept;
  // Buffer must contain complete stereo frames, at most decode_block_frames.
  // Returns frames read and zeroes unused output. An empty buffer is a no-op;
  // otherwise, zero returned frames indicates end of file.
  std::size_t read(std::span<float> output);

private:
  struct Impl;
  explicit StemReader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

struct PreparedSong {
  double sample_rate;
  double duration_seconds;
  std::vector<StemReader> stems;
};

// Prepares direct children only; all stems must share a format and sample rate.
[[nodiscard]] PreparedSong prepare_stems(const std::filesystem::path &folder);

} // namespace playset
