#include "stem_collection.hpp"

#include "fingerprint_encoding.hpp"
#include "macos/fingerprint_source.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace playset {
namespace {
bool is_reference(const std::filesystem::path &path) {
  std::string name;
  for (const unsigned char character : path.stem().string()) {
    if (character == ' ' || (character >= '\t' && character <= '\r')) continue;
    name.push_back(static_cast<char>(character >= 'A' && character <= 'Z'
        ? character + ('a' - 'A') : character));
  }
  while (!name.empty() && name.back() >= '0' && name.back() <= '9') name.pop_back();
  constexpr std::array<std::string_view, 7> references{
      "click", "clicktrack", "clickgroup", "guide", "mdcues", "mdcustom", "mdmyvoice"};
  return std::find(references.begin(), references.end(), name) != references.end();
}
}

std::string StemCollectionFingerprint::hex_digest() const {
  return detail::hex_digest(digest);
}

FingerprintedStems fingerprint_stems(std::span<const std::filesystem::path> paths) {
  if (paths.empty() || paths.size() > maximum_fingerprint_stems)
    throw std::invalid_argument("Fingerprinting requires between 1 and 256 source paths.");
  std::vector<detail::FingerprintSourceVersion> versions;
  std::set<std::filesystem::path> unique_paths;
  for (const auto &path : paths) {
    if (is_reference(path)) continue;
    detail::FingerprintSource source(path);
    if (!unique_paths.insert(source.path()).second)
      throw std::invalid_argument("Fingerprint source paths must be distinct.");
    versions.push_back(source.version());
  }
  if (versions.empty())
    throw std::invalid_argument("Fingerprinting requires at least one non-reference stem.");

  FingerprintedStems result;
  result.stems.reserve(versions.size());
  std::vector<Sha256Digest> digests;
  digests.reserve(versions.size());
  for (const auto &version : versions) {
    version.verify_unchanged();
    const auto fingerprint = fingerprint_stem(version.path);
    result.stems.push_back({version.path, fingerprint});
    digests.push_back(fingerprint.digest);
  }
  for (const auto &version : versions) version.verify_unchanged();

  std::sort(digests.begin(), digests.end());
  detail::Sha256 hash;
  hash.update("playset-stem-collection");
  detail::hash_integer(hash, stem_collection_fingerprint_version);
  detail::hash_integer(hash, stem_fingerprint_version);
  const auto count = static_cast<std::uint32_t>(digests.size());
  detail::hash_integer(hash, count);
  for (const auto &digest : digests) hash.update(digest);
  result.fingerprint = {stem_collection_fingerprint_version, count, hash.finish()};
  return result;
}

bool verify_stems(std::span<const std::filesystem::path> paths,
                  const StemCollectionFingerprint &expected) {
  if (expected.version != stem_collection_fingerprint_version || expected.stem_count == 0 ||
      expected.stem_count > maximum_fingerprint_stems)
    throw std::invalid_argument("Unsupported stem collection fingerprint version or count.");
  const auto actual = fingerprint_stems(paths);
  return actual.fingerprint == expected;
}

} // namespace playset
