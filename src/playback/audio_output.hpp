#pragma once

#include "buffered_playback.hpp"

namespace playset {

// Blocking command-line entry point. Plays once through the current default
// output.
[[nodiscard]] PlaybackReport play_song(PreparedSong song);

} // namespace playset
