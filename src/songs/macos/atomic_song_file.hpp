#pragma once

#include <filesystem>
#include <string_view>

namespace playset::detail {

void write_song_file_atomically(const std::filesystem::path &file, std::string_view text);

} // namespace playset::detail
