#include "song_file.hpp"

#include "macos/atomic_song_file.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace playset {

void save_song_configuration(const std::filesystem::path &file, const SongConfiguration &song) {
  const auto text = serialize_song_configuration(song);
  auto extension = file.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
  });
  if (extension != ".plsong" || file.native().find('\0') != std::filesystem::path::string_type::npos)
    throw std::invalid_argument("Choose a .plsong destination without null characters.");
  detail::write_song_file_atomically(file, text);
}

} // namespace playset
