#pragma once

#include "song_configuration.hpp"

namespace playset {

// Blocking file access. Failed saves leave an existing destination unchanged.
void save_song_configuration(const std::filesystem::path &file, const SongConfiguration &song);

} // namespace playset
