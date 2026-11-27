#include "stem_fingerprint.hpp"

#include "fingerprint_encoding.hpp"
#include "audio/loading/stem_loader.hpp"
#include "macos/fingerprint_source.hpp"
#include "macos/m4a_packets.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>

namespace playset {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

using detail::hash_integer;

void hash_window(detail::Sha256 &hash, StemReader &reader, std::int64_t start,
                 std::int64_t frames) {
  hash_integer(hash, static_cast<std::uint64_t>(start));
  reader.seek(start);
  std::array<float, decode_block_frames * 2> samples{};
  std::array<std::uint8_t, decode_block_frames * 2 * sizeof(float)> bytes{};
  const auto channels = reader.info().channels;
  while (frames > 0) {
    const auto count = static_cast<std::size_t>(std::min<std::int64_t>(frames, decode_block_frames));
    if (reader.read(std::span(samples).first(count * 2)) != count)
      throw std::runtime_error("Source audio ended during fingerprinting: " + reader.info().path.string());
    std::size_t written = 0;
    for (std::size_t frame = 0; frame < count; ++frame) {
      for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto sample = samples[frame * 2 + channel];
        const auto bits = sample == 0 ? std::uint32_t{0} : std::bit_cast<std::uint32_t>(sample);
        for (unsigned byte = 0; byte < sizeof(float); ++byte)
          bytes[written++] = static_cast<std::uint8_t>(bits >> (byte * 8));
      }
    }
    hash.update(std::span(bytes).first(written));
    frames -= static_cast<std::int64_t>(count);
  }
}
}

std::string StemFingerprint::hex_digest() const {
  return detail::hex_digest(digest);
}

StemFingerprint fingerprint_stem(const std::filesystem::path &path) {
  detail::FingerprintSource source(path);
  auto reader = StemReader::open(source.path());
  source.verify_unchanged();
  const auto &info = reader.info();
  const auto window = std::min(info.frames, std::llround(info.sample_rate * stem_sample_seconds));
  const auto available = info.frames - window;
  std::array<std::int64_t, stem_sample_percentages.size()> starts{};
  for (std::size_t i = 0; i < starts.size(); ++i) {
    const auto percent = stem_sample_percentages[i];
    // Split the multiplication so even the largest source frame count fits.
    starts[i] = (available / 100) * percent + ((available % 100) * percent) / 100;
  }
  const auto end = std::unique(starts.begin(), starts.end());
  detail::Sha256 hash;
  hash.update("playset-stem-fingerprint");
  hash_integer(hash, stem_fingerprint_version);
  hash_integer(hash, info.format == StemFormat::wav ? std::uint32_t{0} : std::uint32_t{1});
  hash_integer(hash, std::bit_cast<std::uint64_t>(info.sample_rate));
  hash_integer(hash, info.channels);
  hash_integer(hash, static_cast<std::uint64_t>(info.frames));
  hash_integer(hash, static_cast<std::uint64_t>(window));
  hash_integer(hash, static_cast<std::uint32_t>(end - starts.begin()));
  if (info.format == StemFormat::m4a) {
    detail::M4aPackets packets(info);
    packets.hash_format(hash);
    for (auto start = starts.begin(); start != end; ++start)
      packets.hash_window(hash, *start, window);
  } else {
    for (auto start = starts.begin(); start != end; ++start)
      hash_window(hash, reader, *start, window);
  }
  source.verify_unchanged();
  return {stem_fingerprint_version, hash.finish()};
}

} // namespace playset
