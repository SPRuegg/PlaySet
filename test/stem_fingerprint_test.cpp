#include "audio/fingerprinting/stem_fingerprint.hpp"
#include "audio/fingerprinting/macos/fingerprint_source.hpp"
#include "audio/fingerprinting/macos/sha256.hpp"
#include "audio/loading/stem_loader.hpp"
#include "fixture_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace playset;
using namespace playset::test;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool rejected = false;
  try { action(); } catch (const std::exception &) { rejected = true; }
  require(rejected, "Invalid or changing source was accepted");
}

void change_sample(const fs::path &path, std::uint32_t frame) {
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  file.seekp(44 + frame * 2);
  file.put('\0');
  file.put('\x10');
  require(file.good(), "Could not edit sample fixture");
}

void write_float_zero(const fs::path &path, bool negative, std::uint32_t frames = 1) {
  std::ofstream out(path, std::ios::binary);
  out.write("RIFF", 4);
  write_little_endian(out, 36 + frames * 4, 4);
  out.write("WAVEfmt ", 8);
  write_little_endian(out, 16, 4);
  write_little_endian(out, 3, 2);
  write_little_endian(out, 1, 2);
  write_little_endian(out, 8000, 4);
  write_little_endian(out, 32000, 4);
  write_little_endian(out, 4, 2);
  write_little_endian(out, 32, 2);
  out.write("data", 4);
  write_little_endian(out, frames * 4, 4);
  for (std::uint32_t frame = 0; frame < frames; ++frame)
    write_little_endian(out, negative ? std::uint32_t{0x80000000} : 0, 4);
  require(out.good(), "Could not create float audio fixture");
}

void pcm() {
  TempFolder folder;
  const auto file = folder.path / "stem.wav";
  write_pcm_wav(file, 8000, 1, 16, 1);
  const auto tiny = fingerprint_stem(file);
  require(tiny.version == stem_fingerprint_version && tiny.hex_digest() ==
      "d4a47c9a084d679ea8f39bc2f120457dae42dfb5da33953c77dcdac5791a5bcc",
      "Canonical fingerprint encoding changed");
  detail::Sha256 hash;
  hash.update("abc");
  require(StemFingerprint{1, hash.finish()}.hex_digest() ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
      "SHA-256 differs from its known answer");

  write_pcm_wav(file, 8000, 1, 16, 80000);
  const auto original = fingerprint_stem(file);
  require(original == fingerprint_stem(file), "Repeated WAV reads differ");
  {
    std::ofstream metadata(file, std::ios::binary | std::ios::app);
    metadata.write("JUNK", 4);
    write_little_endian(metadata, 4, 4);
    metadata.write("test", 4);
  }
  {
    std::ofstream header(file, std::ios::binary | std::ios::in);
    header.seekp(4);
    write_little_endian(header, static_cast<std::uint32_t>(fs::file_size(file) - 8), 4);
  }
  require(fingerprint_stem(file) == original, "Unrelated container metadata changed fingerprint");
  change_sample(file, 1);
  require(fingerprint_stem(file) == original, "Unsampled audio unexpectedly changed identity");
  auto active_reader = StemReader::open(file);
  std::array<float, 2> frame{};
  active_reader.read(frame);
  (void)fingerprint_stem(file);
  active_reader.read(frame);
  require(frame[0] == 0.125F, "Fingerprint calculation moved an existing reader");

  fs::create_directory(folder.path / "moved");
  const auto moved = folder.path / "moved" / "Renamed.wav";
  fs::copy_file(file, moved);
  fs::remove(file);
  require(fingerprint_stem(moved) == original, "Renaming or relocation changed fingerprint");
  // Windows begin at frames 7200, 36000, and 64800 in this fixture.
  for (const auto sample : {7200U, 36000U, 64800U}) {
    write_pcm_wav(moved, 8000, 1, 16, 80000);
    change_sample(moved, sample);
    require(fingerprint_stem(moved) != original, "A sampled audio edit was missed");
  }
  write_pcm_wav(moved, 8000, 1, 24, 80000);
  require(fingerprint_stem(moved) == original, "Equivalent decoded PCM depths differ");
  write_pcm_wav(moved, 8000, 1, 16, 80001);
  require(fingerprint_stem(moved) != original, "Frame count was omitted");
  write_pcm_wav(moved, 16000, 1, 16, 80000);
  require(fingerprint_stem(moved) != original, "Sample rate was omitted");
  write_pcm_wav(moved, 8000, 2, 16, 80000);
  require(fingerprint_stem(moved) != original, "Channel count was omitted");

  write_float_zero(file, false);
  const auto zero = fingerprint_stem(file);
  write_float_zero(file, true);
  require(fingerprint_stem(file) == zero, "Signed zero changed fingerprint");
}

bool fingerprint_matches(const fs::path &path, const StemFingerprint &expected) {
  const auto actual = fingerprint_stem(path);
  return actual == expected;
}

void compressed(const fs::path &fixtures) {
  TempFolder folder;
  for (const auto name : {"tone.m4a", "tone-alac.m4a"}) {
    const auto file = fixtures / name;
    const auto original = fingerprint_stem(file);
    for (unsigned attempt = 0; attempt < 8; ++attempt)
      require(fingerprint_matches(file, original), "Compressed audio fingerprint is not repeatable");
    const auto copied = folder.path / name;
    fs::copy_file(file, copied);
    require(fingerprint_stem(copied) == original, "Copied compressed source fingerprint differs");
    require(fingerprint_stem(fixtures / "changed" / name) != original,
            "Changed compressed audio kept its fingerprint");
  }
  auto wav = StemReader::open(fixtures / "tone.wav");
  auto alac = StemReader::open(fixtures / "tone-alac.m4a");
  require(wav.info().sample_rate == alac.info().sample_rate &&
              wav.info().channels == alac.info().channels && wav.info().frames == alac.info().frames,
          "Equivalent lossless fixtures have different source attributes");
  std::array<float, decode_block_frames * 2> pcm{}, lossless{};
  for (auto count = wav.read(pcm); count; count = wav.read(pcm)) {
    require(alac.read(lossless) == count && pcm == lossless,
            "Lossless fixtures do not contain equivalent decoded samples");
  }
  require(fingerprint_stem(fixtures / "tone.wav") != fingerprint_stem(fixtures / "tone-alac.m4a"),
          "Source format was omitted from the fingerprint");
}

void failures() {
  TempFolder folder;
  const auto file = folder.path / "stem.wav";
  rejects([&] { (void)fingerprint_stem(file); });
  rejects([&] { (void)fingerprint_stem(folder.path); });
  { std::ofstream corrupt(file); corrupt << "invalid audio"; }
  rejects([&] { (void)fingerprint_stem(file); });
  write_pcm_wav(file, 8000, 1, 16, 1);
  fs::resize_file(file, 44);
  rejects([&] { (void)fingerprint_stem(file); });

  write_float_zero(file, false, 80000);
  {
    std::ofstream invalid_sample(file, std::ios::binary | std::ios::in);
    invalid_sample.seekp(44 + 7200 * 4);
    write_little_endian(invalid_sample, 0x7fc00000, 4);
  }
  rejects([&] { (void)fingerprint_stem(file); });

  write_pcm_wav(file, 8000, 1, 16, 80000);
  {
    detail::FingerprintSource source(file);
    source.verify_unchanged();
    change_sample(file, 7200);
    rejects([&] { source.verify_unchanged(); });
  }
  {
    detail::FingerprintSource source(file);
    fs::resize_file(file, 44);
    rejects([&] { source.verify_unchanged(); });
  }
  write_pcm_wav(file, 8000, 1, 16, 1);
  {
    detail::FingerprintSource source(file);
    const auto replacement = folder.path / "replacement.wav";
    write_pcm_wav(replacement, 8000, 1, 16, 1);
    fs::rename(replacement, file);
    rejects([&] { source.verify_unchanged(); });
  }
  {
    detail::FingerprintSource source(file);
    fs::remove(file);
    rejects([&] { source.verify_unchanged(); });
  }
}
}

int main(int argc, char *argv[]) {
  try {
    if (argc != 3) throw std::runtime_error("Supply a test case and compressed fixture directory");
    const std::string_view test(argv[1]);
    if (test == "pcm") pcm();
    else if (test == "compressed") compressed(argv[2]);
    else if (test == "failures") failures();
    else throw std::runtime_error("Unknown fingerprint test case");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
