#pragma once

#include "audio/loading/stem_loader.hpp"
#include "sha256.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <vector>

namespace playset::detail {

class M4aPackets {
public:
  explicit M4aPackets(const StemInfo &info);
  ~M4aPackets();
  M4aPackets(const M4aPackets &) = delete;
  M4aPackets &operator=(const M4aPackets &) = delete;
  void hash_format(Sha256 &hash) const;
  void hash_window(Sha256 &hash, std::int64_t start, std::int64_t frames);

private:
  AudioFileID file_ = nullptr;
  std::uint32_t codec_ = 0;
  std::uint32_t frames_per_packet_ = 0;
  std::uint64_t packet_count_ = 0;
  std::uint64_t priming_frames_ = 0;
  std::vector<std::uint8_t> cookie_;
  std::vector<std::uint8_t> packet_;
};

} // namespace playset::detail
