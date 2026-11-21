# StemReader

`StemReader` opens, reads, and seeks within one source audio file. It owns the file's decoder and prepares small chunks of audio for playback without loading the entire stem into memory.

## Definition

Declared in `src/playback/loading/stem_loader.hpp`:

```cpp
class StemReader {
public:
  static StemReader open(const std::filesystem::path &path);
  ~StemReader();
  StemReader(StemReader &&) noexcept;
  StemReader &operator=(StemReader &&) noexcept;
  StemReader(const StemReader &) = delete;
  StemReader &operator=(const StemReader &) = delete;

  [[nodiscard]] const StemInfo &info() const noexcept;
  std::size_t read(std::span<float> output);
  void seek(std::int64_t frame);

private:
  struct Impl;
  explicit StemReader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
```

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `open(path)` | Opens and validates a source file, configures decoding, and returns its reader. |
| `~StemReader()` | Automatically releases the decoder and its file resources when the reader is destroyed. |
| `StemReader(StemReader &&) noexcept`<br>`operator=(StemReader &&) noexcept` | Allows a reader to be moved and ensures that doing so hands off ownership of its decoder and read state without throwing. |
| `StemReader(const StemReader &) = delete`<br>`operator=(const StemReader &) = delete` | Ensures a reader cannot be copied, keeping its decoder and file position owned by one reader at a time. |
| `info()` | Returns a read-only reference to the source's `StemInfo`. |
| `read(output)` | Writes decoded stereo samples into an existing buffer and returns the number of frames read. |
| `seek(frame)` | Changes the source position used by subsequent reads. |


### Opening a stem

```cpp
static StemReader open(const std::filesystem::path &path);
```

By passing path via a `const` reference, it lets `open()` read the caller’s path directly, avoiding an unnecessary copy and guaranteeing it won’t be changed. It still accepts a string literal as well.

Additionally, because `open()` is a static member function, it is called on the class without first creating a reader:

```cpp
auto reader = playset::StemReader::open("/Tracks/Song/Stems/Vocals.wav");
```

Supported sources are:

- PCM WAV files.
- M4A files containing AAC or Apple Lossless (ALAC) audio.
- Mono or stereo audio.
- A finite sample rate between 8,000 and 192,000 Hz, inclusive.
- A positive source frame count and readable decoded audio.

`open()` checks the audio inside the file, rather than relying on its extension.

Opening also reads the first small chunk of audio to confirm it can be decoded. That chunk stays ready for the first `read()`, so opening does not skip the beginning of the stem.

### Accessing source information

```cpp
[[nodiscard]] const StemInfo &info() const noexcept;
```

`info()` lets you look at the file's information without making a copy:

```cpp
const auto &source = reader.info();
const double duration = source.duration_seconds();
```

You can inspect this information, but cannot change it through the returned reference. Calling `info()` does not change the reader or throw an exception. `[[nodiscard]]` asks the compiler to warn if you call it without using the result.

The reference points to information owned by the reader. If you need to keep that information after the reader is destroyed, make a copy instead:

```cpp
const auto source = reader.info();
```

### Reading audio

```cpp
std::size_t read(std::span<float> output);
```

`read()` fills a buffer you provide with the next chunk of audio. Here, `std::span<float>` lets the reader use your buffer and see its size without making another copy of it.

The output is always stereo, arranged as pairs of samples:

```text
left, right, left, right, ...
```

For a mono file, the same sample goes into both channels. For a stereo file, the left and right channels stay separate. The file's sample rate stays the same in either case.

Your buffer must hold complete stereo frames, so its sample count must be even. Each call can read up to `decode_block_frames`, currently **4,096 frames**, which takes **8,192 samples** of space.

```cpp
std::array<float, playset::decode_block_frames * 2> samples{};
const std::size_t frames_read = reader.read(samples);
```

The return value tells you how many **frames** were read, rather than how many individual samples. For example, a return value of `100` means the buffer contains 100 left/right pairs, or 200 samples.

Each read advances the position, so the next call continues where the previous one finished. Near the end of the file, there may be fewer frames left than you requested. The reader returns what is available and fills the rest of your buffer with zeros, producing silence.

Once a nonempty buffer receives zero frames, you have reached the end. Further reads remain silent until you use `seek()` to move back. Passing an empty buffer also returns zero, but does not move the position.

### Seeking

```cpp
void seek(std::int64_t frame);
```

`seek()` chooses where the next read begins. You give it a position in audio frames, rather than seconds. For example, in a file recorded at 44,100 Hz:

```cpp
reader.seek(44100); // The next read begins at one second.
reader.seek(0);     // The next read begins at the start.
```

Negative positions are rejected without changing the current position. A position beyond the file's length moves to its end instead. Reading from the end returns silence; seeking backward makes the audio available again.

Any audio held from the old position is cleared when seeking succeeds, so the next read uses the new position.

This moves one stem. During playback, the transport coordinates seeking across all stems and checks the requested position against the overall song's duration.

## Ownership and lifetime

Each reader owns its decoder and its place in the file. You cannot copy a reader, but you can hand it over to another variable by moving it:

```cpp
auto first = playset::StemReader::open("/Tracks/Song/Stems/Vocals.wav");
auto second = std::move(first);
```

`second` now owns the decoder and continues from the same position. `first` no longer owns it, so you must not call `info()`, `read()`, or `seek()` on `first` unless you assign another reader to it.

The two move declarations marked `noexcept` make this handoff possible without throwing an exception. The two declarations marked `= delete` prevent copying.

When a reader is destroyed, its decoder and file resources are released automatically. Assigning a different reader to it also releases the resources it previously owned. You do not need to call a separate close function.

## Private implementation

```cpp
struct Impl;
explicit StemReader(std::unique_ptr<Impl> impl);
std::unique_ptr<Impl> impl_;
```

`Impl` holds the details needed to read the file: the decoder, source information, a small audio buffer, and the current position within that buffer. These details are defined in `src/playback/loading/macos/stem_reader.cpp`, keeping Apple's audio types out of the public header.

`impl_` owns those details through `std::unique_ptr`, which ensures they belong to one reader and are cleaned up automatically.

The private constructor is used by `open()` once it has prepared the decoder. Callers use `open()` rather than building a reader from those internal details. `explicit` prevents a pointer to those details from being automatically converted into a reader.

## Reading safely

Use a reader from one thread at a time. Opening, reading, and seeking may need to wait for the file or decoder, so playback performs that work on a background thread. The audio callback receives audio that has already been prepared, keeping file access out of the time-sensitive playback work.

If a file cannot be opened or decoded, the reader reports the problem by throwing an exception. Invalid buffer sizes and negative seek positions also throw exceptions. The code using the reader needs to handle these failures.

Opening checks the first chunk of audio, rather than scanning the entire file. A problem farther into the file may therefore be reported by a later `read()`. Samples containing invalid numbers, such as NaN or infinity, are rejected when decoded.

## Example

```cpp
#include "playback/loading/stem_loader.hpp"

#include <array>

auto reader = playset::StemReader::open("/Tracks/Song/Stems/Vocals.wav");
std::array<float, playset::decode_block_frames * 2> samples{};

const std::size_t frames_read = reader.read(samples);
reader.seek(0); // Return to the beginning for the next read.
```

This opens a stem, reads one chunk, and returns to the beginning. Reading prepares the samples; it does not play them through the speakers. The first `frames_read * 2` samples contain the returned audio, and any remaining space is filled with zeros.
