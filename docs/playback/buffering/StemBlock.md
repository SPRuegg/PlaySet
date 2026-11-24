# StemBlock

`StemBlock` holds a small chunk of decoded audio for every track. It keeps the tracks separate so their volumes can still change before the audio is mixed for output.

## Definition

Declared in `src/playback/buffering/stem_block.hpp`:

```cpp
class StemBlock {
public:
  explicit StemBlock(std::size_t track_count);
  [[nodiscard]] std::size_t track_count() const noexcept;
  std::span<float> track(std::size_t index) noexcept;
  std::span<const float> track(std::size_t index) const noexcept;
  std::size_t frames = 0;

private:
  std::vector<float> samples_;
};
```

Implemented in `src/playback/buffering/stem_block.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `StemBlock(track_count)` | Allocates space for a fixed number of tracks. Accepts 1 through 256 tracks. |
| `track_count()` | Returns how many tracks the block holds. |
| `track(index)` | Returns a view of one track's stereo sample buffer. |
| `frames` | Number of valid audio frames in this block, initially `0`. |

### Audio layout

The header defines these constants:

```cpp
inline constexpr std::size_t playback_block_frames = 512;
inline constexpr std::size_t output_channels = 2;
inline constexpr std::size_t playback_prefill_blocks = 3;
```

Each track has space for 512 stereo frames, or 1,024 samples. Samples alternate between left and right:

```text
left, right, left, right, ...
```

All tracks in the block cover the same portion of the song. The `frames` field tells you how many frames are ready to use across those tracks, up to 512. The final block can contain fewer frames.

At 48,000 Hz, a full block represents about 10.67 ms of audio. The duration depends on the song's sample rate; the block itself stores no sample rate.

### Accessing a track

Because `track()` returns a span into the block's existing storage, you can access the samples without copying them. A writable block returns writable samples, while a `const` block returns read-only samples.

The span gives you access to all 1,024 sample slots, even when the block contains less audio. For example, if `frames` is `100`, only the first 200 samples hold the audio to use: 100 left/right pairs.

Track indices start at `0`. For a block with two tracks, you can request track `0` or track `1`. An invalid index triggers an assertion in debug builds; `track()` does not throw an exception to handle that mistake.

Because the span refers to the block's own storage, it must not be used after the block is destroyed or that storage is replaced.

## Allocation and validation

The constructor allocates all sample storage up front. Accessing tracks afterward does not allocate memory.

An invalid track count throws `std::invalid_argument`. You can assign `frames` directly, but the field does not check the value for you. Keep it between `0` and `512` to match the space available.

## Example

```cpp
playset::StemBlock block(2);
auto first_track = block.track(0);

first_track[0] = 0.25F;  // Left sample.
first_track[1] = -0.25F; // Right sample.
block.frames = 1;
```

The block has room for two tracks, with one frame currently marked as valid. Code filling a real block must provide the corresponding audio for every track.
