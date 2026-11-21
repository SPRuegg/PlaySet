#include "fixture_support.hpp"
#include "playback/loading/stem_loader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
using playset::test::TempFolder;
using playset::test::write_pcm_wav;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class F>
void rejects(F &&action, const char *expected_message = nullptr) {
  bool rejected = false;
  try {
    action();
  } catch (const std::exception &error) {
    rejected = true;
    if (expected_message)
      require(std::string(error.what()).find(expected_message) !=
                  std::string::npos,
              "Unexpected rejection reason");
  }
  require(rejected, "Expected rejection");
}

std::vector<float> drain(playset::StemReader &reader,
                         std::size_t chunk = 4096) {
  std::vector<float> result;
  std::vector<float> block(chunk * 2);
  while (true) {
    const auto frames = reader.read(block);
    require(frames <= chunk, "Read exceeded output buffer");
    for (std::size_t i = frames * 2; i < block.size(); ++i) {
      require(block[i] == 0, "Unused output must be silent");
    }
    if (frames == 0)
      break;
    result.insert(result.end(), block.begin(), block.begin() + frames * 2);
    require(result.size() < 1000000, "Decoder did not reach EOF");
  }
  return result;
}

void pcm() {
  TempFolder temp;
  write_pcm_wav(temp.path / "mono.WAV", 48000, 1, 16, 9001);
  write_pcm_wav(temp.path / "stereo.wav", 48000, 2, 24, 10000);
  auto song = playset::prepare_stems(temp.path);
  require(song.stems.size() == 2, "Load both WAVs");
  require(song.stems[0].info().path.filename() == "mono.WAV",
          "Stable filename ordering");
  require(song.duration_seconds == 10000.0 / 48000,
          "Longest stem defines duration");
  const auto mono = drain(song.stems[0], 127);
  require(mono.size() == 18002,
          "Exact mono frame count, no lost priming frames");
  for (const auto sample : mono)
    require(sample == 0.25F, "Mono duplicates to both channels");
  const auto stereo = drain(song.stems[1]);
  require(stereo.size() == 20000, "Exact stereo frame count");
  for (std::size_t i = 0; i < stereo.size(); i += 2) {
    require(stereo[i] == 0.25F && stereo[i + 1] == -0.5F,
            "24-bit stereo channel levels");
  }
  auto reopened = playset::StemReader::open(temp.path / "mono.WAV");
  require(drain(reopened) == mono, "Chunk boundaries do not change audio");
  std::array<float, 1> invalid{};
  rejects([&] { reopened.read(invalid); });
  std::vector<float> oversized(8194);
  rejects([&] { reopened.read(oversized); });
  require(reopened.read({}) == 0, "Empty read is a no-op");
  std::array<float, 8> eof_output;
  eof_output.fill(1.0F);
  require(reopened.read(eof_output) == 0, "Repeated EOF reads remain at EOF");
  require(std::all_of(eof_output.begin(), eof_output.end(),
                      [](float value) { return value == 0; }),
          "Repeated EOF reads produce silence");
  auto unconsumed = playset::StemReader::open(temp.path / "mono.WAV");
  require(unconsumed.read({}) == 0, "Empty read before playback is a no-op");
  require(drain(unconsumed) == mono,
          "Empty read does not discard the primed block");
}

void sample_rates() {
  TempFolder temp;
  write_pcm_wav(temp.path / "mono.wav", 44100, 1, 16, 11025);
  write_pcm_wav(temp.path / "stereo.wav", 44100, 2, 16, 22050);
  auto song = playset::prepare_stems(temp.path);
  require(song.sample_rate == 44100, "Preserve the shared source sample rate");
  require(song.duration_seconds == 0.5, "Preserve duration at the native rate");
  require(drain(song.stems[0]).size() == 22050,
          "Mono remains at its native frame count");
  require(drain(song.stems[1]).size() == 44100,
          "Stereo remains at its native frame count");
  write_pcm_wav(temp.path / "different.wav", 48000, 1, 16, 12000);
  rejects([&] { (void)playset::prepare_stems(temp.path); }, "same sample rate");
  fs::remove(temp.path / "different.wav");
  require(playset::prepare_stems(temp.path).sample_rate == 44100,
          "Recover after mismatched rates");
}

void m4a(const fs::path &fixture) {
  TempFolder temp;
  fs::copy_file(fixture, temp.path / "tone.M4A");
  fs::copy_file(fixture, temp.path / "second.m4a");
  auto song = playset::prepare_stems(temp.path);
  require(song.stems.size() == 2, "Load multiple M4A stems");
  require(song.sample_rate == 44100, "M4A song keeps its source sample rate");
  auto &encoded = song.stems[1];
  require(encoded.info().format == playset::StemFormat::m4a,
          "Inspect the actual M4A container");
  require(encoded.info().channels == 1 && encoded.info().sample_rate == 44100,
          "M4A source metadata");
  require(encoded.info().frames == 11025,
          "Honor M4A encoder delay/padding metadata");
  const auto samples = drain(encoded, 97);
  require(samples.size() / 2 == 11025,
          "M4A decoded duration excludes encoder delay and padding");
  float peak = 0;
  for (std::size_t i = 0; i < samples.size(); i += 2) {
    require(std::isfinite(samples[i]) && samples[i] == samples[i + 1],
            "Finite stereo M4A conversion");
    peak = std::max(peak, std::abs(samples[i]));
  }
  require(peak > 0.1F && peak < 0.4F, "M4A contains the generated tone");
  auto second = playset::StemReader::open(temp.path / "tone.M4A");
  const auto second_samples = drain(second);
  require(second_samples.size() == samples.size(),
          "M4A chunk boundaries preserve frame count");
  float maximum_difference = 0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    maximum_difference =
        std::max(maximum_difference, std::abs(second_samples[i] - samples[i]));
  }
  // Native AAC decoding can differ by floating-point rounding across decoder
  // instances.
  require(maximum_difference <= 0.000001F,
          "M4A priming and chunk boundaries preserve decoded audio");
  TempFolder misnamed;
  fs::copy_file(fixture, misnamed.path / "tone.wav");
  rejects([&] { (void)playset::prepare_stems(misnamed.path); },
          "file extension");
  write_pcm_wav(temp.path / "other.wav", 44100, 1, 16, 11025);
  rejects([&] { (void)playset::prepare_stems(temp.path); }, "not both");
}

void invalid() {
  TempFolder temp;
  rejects([&] { (void)playset::prepare_stems(temp.path / "missing"); });
  rejects([&] { (void)playset::prepare_stems(temp.path); });
  fs::create_directory(temp.path / "nested");
  write_pcm_wav(temp.path / "nested" / "ignored.wav", 48000, 1, 16, 100);
  std::ofstream(temp.path / "notes.txt") << "ignored";
  rejects([&] { (void)playset::prepare_stems(temp.path); });
  write_pcm_wav(temp.path / "a.wav", 48000, 1, 16, 100);
  TempFolder broken_m4a;
  std::ofstream(broken_m4a.path / "broken.m4a") << "not audio";
  rejects([&] { (void)playset::prepare_stems(broken_m4a.path); });
  require(playset::prepare_stems(temp.path).stems.size() == 1,
          "Recover after failed preparation");
  write_pcm_wav(temp.path / "empty.wav", 48000, 1, 16, 0);
  rejects([&] { (void)playset::prepare_stems(temp.path); });
  fs::remove(temp.path / "empty.wav");
  write_pcm_wav(temp.path / "broken.wav", 48000, 1, 16, 100);
  fs::resize_file(temp.path / "broken.wav", 12);
  rejects([&] { (void)playset::prepare_stems(temp.path); });
  fs::remove(temp.path / "broken.wav");
  write_pcm_wav(temp.path / "surround.wav", 48000, 6, 16, 100);
  rejects([&] { (void)playset::prepare_stems(temp.path); });
  fs::remove(temp.path / "surround.wav");
  TempFolder many;
  for (unsigned i = 0; i < 257; ++i)
    std::ofstream(many.path / (std::to_string(i) + ".wav"));
  rejects([&] { (void)playset::prepare_stems(many.path); });
}
} // namespace

int main(int argc, char *argv[]) {
  try {
    require(argc == 3, "Expected test case and M4A fixture path");
    const std::string name = argv[1];
    if (name == "pcm")
      pcm();
    else if (name == "sample_rates")
      sample_rates();
    else if (name == "m4a")
      m4a(argv[2]);
    else if (name == "invalid")
      invalid();
    else
      throw std::runtime_error("Unknown test case");
    std::cout << "PASS: " << name << '\n';
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
