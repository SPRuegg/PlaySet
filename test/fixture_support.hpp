#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace playset::test {
namespace fs = std::filesystem;

struct TempFolder {
  fs::path path;
  TempFolder() {
    auto pattern = (fs::temp_directory_path() / "playset-test-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const auto created = mkdtemp(writable.data());
    if (!created)
      throw std::runtime_error("Could not create test folder");
    path = created;
  }
  TempFolder(const TempFolder &) = delete;
  TempFolder &operator=(const TempFolder &) = delete;
  ~TempFolder() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

inline void write_little_endian(std::ofstream &stream, std::uint32_t value,
                                unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i)
    stream.put(static_cast<char>((value >> (i * 8)) & 255));
}

inline void write_pcm_wav(const fs::path &path, std::uint32_t rate,
                          unsigned channels, unsigned bits,
                          std::uint32_t frames) {
  std::ofstream out(path, std::ios::binary);
  const auto bytes = frames * channels * (bits / 8);
  out.write("RIFF", 4);
  write_little_endian(out, 36 + bytes, 4);
  out.write("WAVEfmt ", 8);
  write_little_endian(out, 16, 4);
  write_little_endian(out, 1, 2);
  write_little_endian(out, channels, 2);
  write_little_endian(out, rate, 4);
  write_little_endian(out, rate * channels * (bits / 8), 4);
  write_little_endian(out, channels * (bits / 8), 2);
  write_little_endian(out, bits, 2);
  out.write("data", 4);
  write_little_endian(out, bytes, 4);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    for (unsigned channel = 0; channel < channels; ++channel) {
      // Exactly +0.25 left/mono and -0.5 right, including frame zero.
      const auto sample =
          channel == 0 ? (1U << (bits - 3)) : (3U << (bits - 2));
      write_little_endian(out, sample, bits / 8);
    }
  }
  if (!out.good())
    throw std::runtime_error("Could not generate WAV fixture");
}

} // namespace playset::test
