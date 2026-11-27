# Stem collection fingerprints

Use a collection fingerprint when you want to identify several stems together. It combines their individual fingerprints without depending on their order, filenames, or folder location, after leaving out the reference tracks described below.

## Definition

Declared in `src/audio/fingerprinting/stem_collection.hpp`:

```cpp
struct StemCollectionFingerprint {
  std::uint32_t version = stem_collection_fingerprint_version;
  std::uint32_t stem_count = 0;
  Sha256Digest digest{};
  [[nodiscard]] std::string hex_digest() const;
  bool operator==(const StemCollectionFingerprint &) const = default;
};

struct FingerprintedStem {
  std::filesystem::path path;
  StemFingerprint fingerprint;
};

struct FingerprintedStems {
  StemCollectionFingerprint fingerprint;
  std::vector<FingerprintedStem> stems;
};

[[nodiscard]] FingerprintedStems fingerprint_stems(
    std::span<const std::filesystem::path> paths);
[[nodiscard]] bool verify_stems(
    std::span<const std::filesystem::path> paths,
    const StemCollectionFingerprint &expected);
```

The implementation lives in `src/audio/fingerprinting/stem_collection.cpp`. Both functions belong to `PlaySet::Audio` and use their own readers. You can call them without creating a transport or changing a reader already in use.

| Field or declaration | What you can do |
| --- | --- |
| `version` | Identify the collection's combination and exclusion rules. The current version is `1`. |
| `stem_count` | See how many stems contributed to the fingerprint, including repeated audio in distinct source files. |
| `digest` | Access the 32 SHA-256 bytes for the whole collection. |
| `hex_digest()` | Display those bytes as 64 lowercase hexadecimal characters. |
| `operator==` | Compare version, included stem count, and digest. |
| `FingerprintedStem::path` | Find the canonical source path associated with an individual fingerprint. |
| `FingerprintedStem::fingerprint` | Inspect that source's [StemFingerprint](StemFingerprint.md). |
| `FingerprintedStems::fingerprint` | Read the completed collection fingerprint. |
| `FingerprintedStems::stems` | Read the included sources and individual fingerprints in the order you supplied them. |
| `fingerprint_stems(paths)` | Calculate a complete collection result. |
| `verify_stems(paths, expected)` | Calculate the current fingerprint and return whether it matches the expected one. |

## Calculating and comparing

```cpp
#include "audio/fingerprinting/stem_collection.hpp"

#include <array>

const std::array<std::filesystem::path, 2> sources{
    "/Tracks/Song/Stems/Bass.wav",
    "/Tracks/Song/Stems/Piano.wav"
};
const auto result = playset::fingerprint_stems(sources);
const bool matches = playset::verify_stems(sources, result.fingerprint);
```

Here, `result.fingerprint` identifies the two stems together. `result.stems[0]` belongs to Bass and `result.stems[1]` belongs to Piano. Reversing the inputs keeps the collection fingerprint the same but reverses those returned source records, so you can still associate each fingerprint with its source.

The functions accept source paths rather than discovering files in a folder. Preparing or playing a song is a separate operation. Fingerprinting does not enforce the playback loader's shared-format or shared-sample-rate rules; it identifies the supplied sources using each file's own attributes.

## Reference tracks

Reference tracks stay available for playback, but do not contribute to collection identity. Replacing one with your own click or guide therefore does not make an otherwise unchanged collection mismatch.

Version `1` excludes these filename stems:

- Click, Click Track, and Click Group.
- Guide, MD Cues, MD Custom, and MD My Voice.

The check ignores ASCII letter case and whitespace, then removes trailing digits. For example, `CLICK TRACK 42.wav`, `Guide01.m4a`, and `MD My Voice 5.wav` are excluded. Names such as `Guided Guitar` and `Click Claps` stay included because they do not match a whole reference name after that normalization.

Only the final filename without its extension is checked. Folder names do not influence exclusion. Renaming a musical stem to a reference name changes which sources are included; other renames leave identity unchanged.

Excluded paths are not opened or decoded, and they do not appear in `result.stems`. Checking whether those reference files can actually be played remains the loader's responsibility. The individual `fingerprint_stem()` function still fingerprints any supported file you explicitly give it.

## Duplicate audio and input limits

You can supply between 1 and 256 paths, counting references. At least one must be a non-reference stem. Empty, oversized, and reference-only collections throw `std::invalid_argument` rather than producing an identity with no audio behind it.

Every included path must resolve to a distinct canonical path. Listing the same path twice, or listing both a file and a symbolic link to it, throws `std::invalid_argument`.

Different files with identical sampled audio are allowed. They keep separate source records and contribute separate digest entries. For example, adding a copied Bass stem changes the collection count and fingerprint even though its individual fingerprint matches the original Bass.

## How the collection is combined

The function calculates an individual fingerprint for every included source, then sorts a copy of the digest bytes. Equal digests are retained. It hashes the following values in order:

| Order | Value and encoding |
| --- | --- |
| 1 | ASCII `playset-stem-collection`, without a trailing zero byte. |
| 2 | Collection version as an unsigned 32-bit integer. |
| 3 | Individual stem fingerprint version as an unsigned 32-bit integer. |
| 4 | Included stem count as an unsigned 32-bit integer. |
| 5 | The sorted individual SHA-256 digests, 32 raw bytes each. |

Numeric fields use least-significant-byte-first order. Digests sort by byte value, from their first byte onward. Paths, filenames, and timestamps are not hashed. Changing the sampling rules, combination rules, or reference policy requires the corresponding version to change.

## Mismatches and calculation failures

`verify_stems()` returns `true` for a match and `false` when a completed calculation differs. A missing, unreadable, corrupt, or changing included source throws instead of returning `false`. An unsupported expected version, or an expected count outside 1 through 256, throws `std::invalid_argument`.

Verification recalculates the fingerprints and does not use a cache. A different stem count does not skip reading the included sources, so a count mismatch cannot hide a file error.

The collection records each included file's identity and metadata before sampling begins, then checks them again after all stems have been sampled. If an earlier file changes while another is being read, those checks can reject the whole result. The records do not keep every file descriptor open at once.

No partial result is returned if a calculation fails. As with individual fingerprints, these are observations of the files rather than a locked, atomic snapshot. The same [sampling limits](StemFingerprint.md#cost-and-limits) apply: edits outside the sampled windows may be missed.

## Calling safely

Both functions perform blocking file access and decoding. Call them outside the audio callback. Their memory use is bounded by the 256-path limit and each individual fingerprint's buffers; they do not load every stem's complete audio into memory or write to source files.
