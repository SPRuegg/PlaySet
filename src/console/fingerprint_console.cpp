#include "fingerprint_console.hpp"

#include "audio/fingerprinting/stem_collection.hpp"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace playset {

void inspect_stem_fingerprints(const std::filesystem::path &folder, std::ostream &output) {
  if (!std::filesystem::is_directory(folder))
    throw std::runtime_error("Stem folder does not exist or is not a directory: " + folder.string());

  std::vector<std::filesystem::path> paths;
  for (const auto &entry : std::filesystem::directory_iterator(folder)) {
    if (!entry.is_regular_file()) continue;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    if (extension != ".wav" && extension != ".m4a") continue;
    paths.push_back(entry.path());
    if (paths.size() > maximum_fingerprint_stems)
      throw std::invalid_argument("Fingerprinting requires between 1 and 256 source paths.");
  }
  if (paths.empty()) throw std::runtime_error("No WAV or M4A stems found in: " + folder.string());
  std::sort(paths.begin(), paths.end());
  const auto result = fingerprint_stems(paths);

  output << "Stem collection fingerprint v" << result.fingerprint.version << ": "
         << result.fingerprint.hex_digest() << '\n'
         << "Included stems: " << result.stems.size() << '\n'
         << "Excluded references: " << paths.size() - result.stems.size() << '\n'
         << "Individual fingerprint version: " << stem_fingerprint_version << '\n';
  for (const auto &stem : result.stems)
    output << stem.fingerprint.hex_digest() << "  " << stem.path.filename().string() << '\n';
}

} // namespace playset
