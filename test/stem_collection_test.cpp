#include "audio/fingerprinting/stem_collection.hpp"
#include "audio/fingerprinting/macos/fingerprint_source.hpp"
#include "fixture_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace playset;
using namespace playset::test;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::exception &) { rejected = true; }
  require(rejected, "Expected a calculation failure rather than a match result");
}

struct Sources {
  TempFolder folder;
  std::array<fs::path, 2> paths{folder.path / "Bass.wav", folder.path / "Piano.wav"};
  Sources() {
    for (const auto &path : paths) write_pcm_wav(path, 8000, 1, 16, 1);
    std::ofstream changed(paths[1], std::ios::binary | std::ios::in);
    changed.seekp(44);
    write_little_endian(changed, 4096, 2);
  }
};

void identity() {
  Sources source;
  const auto original = fingerprint_stems(source.paths);
  require(original.fingerprint.stem_count == 2 && original.stems.size() == 2,
          "Collection lost its included count");
  require(original.stems[0].path == fs::canonical(source.paths[0]) &&
              original.stems[1].path == fs::canonical(source.paths[1]) &&
              original.stems[0].fingerprint != original.stems[1].fingerprint,
          "Individual fingerprints lost their source association");
  require(verify_stems(source.paths, original.fingerprint), "Unchanged collection failed verification");
  const std::array reversed{source.paths[1], source.paths[0]};
  const auto reordered = fingerprint_stems(reversed);
  require(reordered.fingerprint == original.fingerprint &&
              reordered.stems[0].fingerprint == original.stems[1].fingerprint,
          "Collection depends on order or sorts the returned source mapping");

  const auto relocated = source.folder.path / "elsewhere";
  fs::create_directory(relocated);
  const std::array moved{relocated / "Renamed A.wav", relocated / "Renamed B.wav"};
  for (std::size_t i = 0; i < moved.size(); ++i) fs::rename(source.paths[i], moved[i]);
  require(verify_stems(moved, original.fingerprint), "Renaming or moving sources changed collection identity");
  const auto one = std::span(moved).first(1);
  require(!verify_stems(one, original.fingerprint), "Removing a stem did not mismatch");
  require(fingerprint_stems(one).fingerprint.hex_digest() ==
      "97c193b3242e811275963190a5c9db4889410c7c16b2ace65dd1be5b175871d0",
      "Collection byte encoding changed");

  const auto duplicate = relocated / "Another Bass.wav";
  fs::copy_file(moved[0], duplicate);
  const std::array duplicates{moved[0], duplicate};
  const auto repeated = fingerprint_stems(duplicates);
  require(repeated.fingerprint.stem_count == 2 &&
              repeated.stems[0].fingerprint == repeated.stems[1].fingerprint &&
              repeated.fingerprint != fingerprint_stems(one).fingerprint,
          "Identical-content stems were deduplicated");
  const std::array added{moved[0], moved[1], duplicate};
  require(!verify_stems(added, original.fingerprint), "Adding a duplicate-content stem did not mismatch");
  require(!verify_stems(duplicates, original.fingerprint), "Replacing a stem with duplicate audio did not mismatch");

  write_pcm_wav(moved[1], 8000, 1, 16, 2);
  require(!verify_stems(moved, original.fingerprint), "Changing stem duration did not mismatch");
}

void exclusions() {
  Sources source;
  const auto original = fingerprint_stems(source.paths);
  for (const auto name : {"Click.wav", "CLICK TRACK 42.wav", "Click Group2.wav", "guide01.wav",
                          "MD Cues 99.m4a", "MD Custom 3.wav", "MD My Voice5.wav", " Click\t 7.wav"}) {
    const auto reference = source.folder.path / name;
    write_pcm_wav(reference, 8000, 1, 16, 1);
    const std::array paths{source.paths[0], reference, source.paths[1]};
    require(verify_stems(paths, original.fingerprint), "Reference stem changed collection identity");
    { std::ofstream replacement(reference); replacement << "custom reference"; }
    require(verify_stems(paths, original.fingerprint), "Changed reference content was sampled");
    fs::remove(reference);
    require(verify_stems(paths, original.fingerprint), "Excluded reference path was opened");
  }
  for (const auto name : {"Guided Guitar.wav", "Click Claps.wav", "Band.wav", "MD Piano.wav"}) {
    const auto musical = source.folder.path / name;
    write_pcm_wav(musical, 8000, 1, 16, 1);
    const std::array paths{source.paths[0], musical, source.paths[1]};
    const auto result = fingerprint_stems(paths);
    require(result.fingerprint.stem_count == 3 && result.fingerprint != original.fingerprint,
            "A partial name match incorrectly excluded a stem");
  }
  const std::array references{source.folder.path / "Click.wav", source.folder.path / "Guide.wav"};
  rejects([&] { (void)fingerprint_stems(references); });
}

void failures() {
  Sources source;
  const auto original = fingerprint_stems(source.paths);
  rejects([&] { (void)fingerprint_stems({}); });
  const std::array same_path{source.paths[0], source.folder.path / "." / "Bass.wav"};
  rejects([&] { (void)fingerprint_stems(same_path); });
  const auto link = source.folder.path / "Bass link.wav";
  fs::create_symlink(source.paths[0], link);
  const std::array same_source{source.paths[0], link};
  rejects([&] { (void)fingerprint_stems(same_source); });

  const std::array missing{source.paths[0], source.folder.path / "missing.wav"};
  rejects([&] { (void)verify_stems(missing, original.fingerprint); });
  const auto corrupt = source.folder.path / "bad.wav";
  { std::ofstream file(corrupt); file << "invalid source"; }
  const std::array invalid{source.paths[0], source.paths[1], corrupt};
  rejects([&] { (void)verify_stems(invalid, original.fingerprint); });
  auto unchanged = original;
  rejects([&] { unchanged = fingerprint_stems(invalid); });
  require(unchanged.fingerprint == original.fingerprint && unchanged.stems.size() == 2,
          "Failed calculation published a partial result");

  for (const auto version : {0U, stem_collection_fingerprint_version + 1}) {
    auto expected = original.fingerprint;
    expected.version = version;
    rejects([&] { (void)verify_stems(source.paths, expected); });
  }
  for (const auto count : {0U, 257U}) {
    auto expected = original.fingerprint;
    expected.stem_count = count;
    rejects([&] { (void)verify_stems(source.paths, expected); });
  }
  auto mismatched = original.fingerprint;
  mismatched.digest[0] ^= 1;
  require(!verify_stems(source.paths, mismatched), "Digest mismatch did not return false");

  const auto version = detail::FingerprintSource(source.paths[0]).version();
  version.verify_unchanged();
  fs::resize_file(source.paths[0], 44);
  rejects([&] { version.verify_unchanged(); });
}

void limits() {
  TempFolder folder;
  std::vector<fs::path> paths;
  for (std::size_t i = 0; i < maximum_fingerprint_stems; ++i) {
    const auto path = folder.path / ("Stem " + std::to_string(i) + ".wav");
    write_pcm_wav(path, 8000, 1, 16, 1);
    paths.push_back(path);
  }
  const auto result = fingerprint_stems(paths);
  require(result.fingerprint.stem_count == maximum_fingerprint_stems && result.stems.size() == paths.size(),
          "Maximum-size collection lost repeated-content stems");
  paths.push_back(folder.path / "extra.wav");
  rejects([&] { (void)fingerprint_stems(paths); });
}

void compressed(const fs::path &fixtures) {
  TempFolder folder;
  const std::array paths{fixtures / "tone.m4a", fixtures / "tone-alac.m4a"};
  const auto result = fingerprint_stems(paths);
  require(result.fingerprint.stem_count == 2, "Compressed collection lost its included count");
  for (unsigned attempt = 0; attempt < 8; ++attempt)
    require(verify_stems(paths, result.fingerprint), "AAC and ALAC collection verification failed");
  const std::array copied{folder.path / "Piano.m4a", folder.path / "Bass.m4a"};
  fs::copy_file(paths[1], copied[0]);
  fs::copy_file(paths[0], copied[1]);
  require(verify_stems(copied, result.fingerprint), "Relocated compressed collection did not match");
  for (const auto &path : copied) {
    std::ofstream metadata(path, std::ios::binary | std::ios::app);
    const std::array<char, 16> atom{0, 0, 0, 16, 'f', 'r', 'e', 'e', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a'};
    metadata.write(atom.data(), atom.size());
  }
  require(verify_stems(copied, result.fingerprint), "Unrelated M4A container data changed identity");
}
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 3) throw std::runtime_error("Supply a test case and fixture directory");
    const std::string_view test(argv[1]);
    if (test == "identity") identity();
    else if (test == "exclusions") exclusions();
    else if (test == "failures") failures();
    else if (test == "limits") limits();
    else if (test == "compressed") compressed(argv[2]);
    else throw std::runtime_error("Unknown collection test case");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
