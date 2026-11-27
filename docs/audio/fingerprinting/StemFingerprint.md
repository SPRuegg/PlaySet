# StemFingerprint

`StemFingerprint` identifies one source audio file using its technical information and small samples of its audio. You can calculate it without loading the entire recording into memory or starting playback.

## Definition

Declared in `src/audio/fingerprinting/stem_fingerprint.hpp`:

```cpp
struct StemFingerprint {
  std::uint32_t version = stem_fingerprint_version;
  Sha256Digest digest{};
  [[nodiscard]] std::string hex_digest() const;
  bool operator==(const StemFingerprint &) const = default;
};

[[nodiscard]] StemFingerprint fingerprint_stem(const std::filesystem::path &path);
```

Link your target to `PlaySet::Audio` to use this function. The implementation lives in `src/audio/fingerprinting/stem_fingerprint.cpp`; macOS hashing and file checks stay in its `macos` subfolder.

| Field or declaration | What you can do |
| --- | --- |
| `version` | Identify the sampling and encoding rules used to produce the digest. The current version is `1`. |
| `digest` | Access the 32 SHA-256 digest bytes. `Sha256Digest` is an alias for `std::array<std::uint8_t, 32>`. |
| `hex_digest()` | Get those bytes as 64 lowercase hexadecimal characters. |
| `operator==` | Compare both the version and digest. |
| `fingerprint_stem(path)` | Open one source file and return its completed fingerprint, or throw if the calculation fails. |

## Calculating a fingerprint

```cpp
#include "audio/fingerprinting/stem_fingerprint.hpp"

const auto fingerprint = playset::fingerprint_stem("/Tracks/Song/Stems/Bass.wav");
const auto text = fingerprint.hex_digest();
```

You can pass a relative or absolute path. The function opens its own [StemReader](../loading/StemReader.md), so it does not move any reader already being used for playback. It accepts the same PCM WAV and AAC/ALAC M4A sources as the reader.

The function reads from the file and may wait for its decoder or storage device. Call it from a background or other non-audio thread, never from an audio callback. It has no cache and does not write to the source.

## What gets sampled

Version `1` uses three one-second windows. Their starting positions are 10%, 50%, and 90% of the range where a complete window fits.

For a ten-second recording, the windows start at approximately `0.9`, `4.5`, and `8.1` seconds. Positions round down to a whole source frame. The calculation uses integer arithmetic so long recordings do not lose frame precision.

If a recording is shorter than one second, its window covers the whole recording. Identical starting positions are sampled once. Windows can overlap in short recordings.

The hash includes:

- The fingerprint version and source format category: WAV or M4A.
- The source sample rate, channel count, and total frame count.
- The window length, number of distinct windows, and their starting frames.
- For WAV, the decoded audio in those windows at the original sample rate.
- For M4A, the encoded packets covering those windows, plus the codec configuration needed to interpret them.

The filename and folder path are left out. Renaming or copying an unchanged file therefore keeps its fingerprint. Extra container metadata is also left out unless it changes the sampled audio, codec configuration, or one of the source attributes above.

For WAV, mono files contribute one sample per frame and stereo files contribute both channels. The reader duplicates mono audio for playback, but that duplicated channel is not hashed a second time.

For M4A, a packet is a small encoded piece of audio. The function reads the complete packets covering each window, including any audio just outside the window in its first and last packets. It accounts for encoder priming frames when finding those packets. Using the encoded packets avoids tiny differences that AAC decoders can produce in the lowest bits of floating-point samples.

## Consistent byte encoding

Hashing a C++ object's memory directly could include padding or depend on the machine's byte order. Instead, each field is written in a defined order and encoding. WAV audio samples use 32-bit IEEE floating-point values with the least significant byte first. Positive and negative zero both become positive zero. The reader rejects non-finite audio samples.

Different PCM bit depths can match when they decode to the same samples and the included source attributes match. WAV and M4A remain distinct format categories even when their audio is lossless and equivalent. Re-encoding a file is not guaranteed to keep its fingerprint.

The exact version `1` input to SHA-256 is:

| Order | Value and encoding |
| --- | --- |
| 1 | ASCII `playset-stem-fingerprint`, without a trailing zero byte. |
| 2 | Version as an unsigned 32-bit integer. |
| 3 | Format as an unsigned 32-bit integer: `0` for WAV, `1` for M4A. |
| 4 | Sample rate as a 64-bit IEEE floating-point value. |
| 5 | Source channel count as an unsigned 32-bit integer. |
| 6 | Source frame count and window frame count, each as an unsigned 64-bit integer. |
| 7 | Distinct window count as an unsigned 32-bit integer. |
| 8a, WAV | For each window: its starting frame as an unsigned 64-bit integer, then its decoded samples in source-channel order. |
| 8b, M4A | Codec ID and frames per packet, each unsigned 32-bit; priming frames as unsigned 64-bit; then codec cookie byte count as unsigned 32-bit and its raw bytes. |
| 9, M4A | For each window: starting frame, first packet index, and packet count, each unsigned 64-bit. For each packet: byte count as unsigned 32-bit, then raw packet bytes. |

All numeric fields use least-significant-byte-first order. Changes to these rules require a new fingerprint version. M4A packet data is read without decoding the sampled windows. The codec cookie contains configuration needed to decode the packets; artwork and descriptive tags are not included.

## File changes and failures

The function checks the file's device, identity, size, modification time, and change time before and after sampling. It keeps an open file descriptor while checking the current path as well, so replacement or removal can be detected even if the original file remains open.

If a file changes during the observed checks, disappears, cannot be read, or ends before a requested window is complete, the function throws an exception instead of returning a partial result. These checks catch ordinary source changes; they do not lock the file or provide an atomic snapshot of its contents.

## Cost and limits

WAV sampling hashes at most three seconds of decoded audio in chunks of up to 4,096 frames. Its sample and encoding buffers use about 64 KiB, plus the reader's own memory.

M4A sampling reads one packet at a time. Packet storage and the codec cookie are each limited to 1 MiB; oversized or unsupported packet layouts throw. Opening a reader still decodes its first block to validate the source. Packet boundaries and internal file reads can add some work beyond the three sampled seconds, so the window length does not guarantee disk bytes read or elapsed time.

This is a sampled fingerprint, not a full-file checksum. Changes outside the sampled windows may go undetected. Sparse tracks may contain silence in every sampled window, so matching fingerprints are evidence of a match rather than proof that every sample is identical.

The function fingerprints exactly the one file you supply. It does not choose files based on their names.
