#pragma once

#include "song_configuration.hpp"

#include <cstddef>

namespace playset {
class Transport;

class SongConfigurationCapture {
public:
  SongConfigurationCapture(Transport &transport, const std::filesystem::path &stem_folder);
  SongConfigurationCapture(const SongConfigurationCapture &) = delete;
  SongConfigurationCapture &operator=(const SongConfigurationCapture &) = delete;
  [[nodiscard]] SongConfiguration capture();

private:
  struct Source {
    std::size_t track_id;
    std::string id;
    std::filesystem::path path;
  };

  Transport &transport_;
  std::string id_;
  std::filesystem::path stem_folder_;
  std::vector<Source> sources_;
  std::optional<StemCollectionFingerprint> expected_;
};

} // namespace playset
