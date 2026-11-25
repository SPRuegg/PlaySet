# PreparedSong

`PreparedSong` groups the opened stem readers for one song. It stores their shared sample rate, the song's duration, and the readers needed to access its audio.

## Definition

Declared in `src/playback/loading/stem_loader.hpp`:

```cpp
struct PreparedSong {
  double sample_rate;
  double duration_seconds;
  std::vector<StemReader> stems;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `sample_rate` | `double` | Number of audio frames per second, shared by all stems. |
| `duration_seconds` | `double` | Duration of the longest stem, in seconds. |
| `stems` | `std::vector<StemReader>` | Opened readers for the song's source audio files. |

### Shared sample rate

All stems in a prepared song use the same sample rate. For example, if the song's sample rate is 44,100 Hz, each stem contains 44,100 frames per second of audio.

The original sample rate is preserved. Preparing the song does not convert its stems to another rate.

### Song duration

Stems can have different lengths. The song's duration follows the longest one, allowing shorter stems to finish before the song ends.

For example, if the drums last 180 seconds and the vocals last 175 seconds, `duration_seconds` is `180.0`.

This value is stored when the song is prepared. It is not a function that recalculates the duration whenever you access it. If code later changes the collection of stems, it must also keep the stored duration and sample rate correct.

### Stem readers

`std::vector<StemReader>` holds the readers together in an ordered collection. Each reader owns its decoder and reads its file in small chunks, so `PreparedSong` does not contain the entire song's decoded audio in memory.

The readers are kept in sorted file-path order when the song is prepared. Each reader's `info()` provides the information for its source file.

Because readers cannot be copied, the prepared song cannot be copied either. It can be moved to transfer ownership of its readers. When those readers are destroyed, their decoder and file resources are released automatically.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");

const double sample_rate = song.sample_rate;
const double duration = song.duration_seconds;
const std::size_t stem_count = song.stems.size();

const auto &first_stem = song.stems.front().info();
```

Here, `song` holds the opened readers and the information shared across the song. Accessing `first_stem` lets you inspect the first source file without copying its information.

`PreparedSong` itself does not validate its fields. In this example, `prepare_stems()` supplies a nonempty collection of compatible stems and calculates the song's duration before returning it.
