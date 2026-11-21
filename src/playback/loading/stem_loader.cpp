#include "stem_loader.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace playset {
namespace {
constexpr std::size_t maximum_stems = 256;
}

double StemInfo::duration_seconds() const noexcept {
  return static_cast<double>(frames) / sample_rate;
}

PreparedSong prepare_stems(const std::filesystem::path &folder) {
  if (!std::filesystem::is_directory(folder)) {
    throw std::runtime_error(
        "Stem folder does not exist or is not a directory: " + folder.string());
  }

  std::vector<std::filesystem::path> files;
  std::string song_extension;
  for (const auto &entry : std::filesystem::directory_iterator(folder)) {
    if (!entry.is_regular_file())
      continue;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(
                         c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
                   });
    if (extension == ".wav" || extension == ".m4a") {
      if (!song_extension.empty() && extension != song_extension) {
        throw std::runtime_error(
            "Use either WAV stems or M4A stems, not both in the same folder.");
      }
      song_extension = extension;
      files.push_back(entry.path());
      if (files.size() > maximum_stems)
        throw std::runtime_error("A song supports at most 256 stems.");
    }
  }
  if (files.empty())
    throw std::runtime_error("No WAV or M4A stems found in: " +
                             folder.string());
  std::sort(files.begin(), files.end());

  PreparedSong song{0.0, 0.0, {}};
  song.stems.reserve(files.size());
  for (const auto &file : files) {
    auto reader = StemReader::open(file);
    const auto expected_format =
        song_extension == ".wav" ? StemFormat::wav : StemFormat::m4a;
    if (reader.info().format != expected_format) {
      throw std::runtime_error(
          "Stem contents do not match its file extension: " + file.string());
    }
    if (song.stems.empty())
      song.sample_rate = reader.info().sample_rate;
    if (reader.info().sample_rate != song.sample_rate) {
      throw std::runtime_error(
          "All stems must use the same sample rate. Mismatched stem: " +
          file.string());
    }
    song.duration_seconds =
        std::max(song.duration_seconds, reader.info().duration_seconds());
    song.stems.push_back(std::move(reader));
  }
  return song;
}

} // namespace playset
