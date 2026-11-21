# StemInfo

`StemInfo` describes one source audio file's source attributes. It stores the file’s location, format, sample rate, channel count, and total frame count.

## Definition

Declared in `src/playback/loading/stem_loader.hpp`:

```cpp
struct StemInfo {
  std::filesystem::path path;
  StemFormat format;
  double sample_rate;
  std::uint32_t channels;
  std::int64_t frames;

  [[nodiscard]] double duration_seconds() const noexcept {
    return static_cast<double>(frames) / sample_rate;
  }
};
```

## Fields

| Field         | Type                    | Purpose                                                                           |
| ------------- | ----------------------- | --------------------------------------------------------------------------------- |
| `path`        | `std::filesystem::path` | Location of the source audio file supplied as a canonical path.  |
| `format`      | `StemFormat`            | Source format category: `StemFormat::wav` or `StemFormat::m4a`.                   |
| `sample_rate` | `double`                | Number of source audio frames per second.                                         |
| `channels`    | `std::uint32_t`         | Number of channels in the source file. Currently `StemReader` only supports mono or stereo. |
| `frames`      | `std::int64_t`         | Total number of audio frames in the source file.                                  |

### Audio frames

An audio frame contains one sample for each channel.

- A mono frame contains one sample.
- A stereo frame contains two samples: left and right.

Consequently, a stereo file with 44,100 frames contains 88,200 individual samples. At a sample rate of 44,100 Hz, that file lasts one second.

The `channels` field describes the source file and does **not** describe `StemReader`’s output layout, which is always stereo.

### Integer types

The channel count uses a 32-bit unsigned integer to match Apple’s audio API representation. Using this type does not imply support for billions of channels.

The frame count uses a signed 64-bit integer to match Apple’s audio API representation. However, `StemReader` will only accept **positive** values for frame count.

## Calculating duration

```cpp
[[nodiscard]] double duration_seconds() const noexcept {
  return static_cast<double>(frames) / sample_rate;
}
```

Duration is calculated from the source information rather than stored separately:

The function is defined directly inside the struct. Such member functions are implicitly inline, allowing this definition to appear in a header included by multiple source files.

The function is qualified with `[[nodiscard]]` to warn against using this function without using or storing its duration.

## Example

```cpp
playset::StemInfo info{
    "/Tracks/Song/Stems/Vocals.wav",
    playset::StemFormat::wav,
    44100.0,
    2,
    132300
};

const double duration = info.duration_seconds(); // 3.0
```
