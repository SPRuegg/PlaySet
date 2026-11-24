# Block functions

Two functions fill and mix `StemBlock` audio. They keep decoding on the background thread while leaving volume changes available until playback renders the samples.

Declared in `src/playback/buffering/stem_block.hpp`.

## Decoding a block

```cpp
std::size_t decode_stem_block(PreparedSong &song, StemBlock &block);
```

`decode_stem_block()` reads the next chunk from every stem into its corresponding track buffer. The song and block must have matching track counts; otherwise, the function throws `std::invalid_argument`.

The function sets `block.frames` to the largest number of frames returned by any stem and returns that same count. Because each reader pads unused space with zeros, a shorter stem contributes silence while longer stems continue. A return value of `0` means every reader has reached its end.

## Mixing a block

```cpp
std::uint64_t mix_stem_block(const StemBlock &block, std::size_t first_frame,
                            std::span<float> output,
                            VolumeSmoother *volume = nullptr) noexcept;
```

`mix_stem_block()` combines the tracks in a block into one left and right output. You choose where to start with `first_frame`. For example, `0` starts at the beginning of the block, while `100` skips its first 100 frames.

The size of `output` determines how much audio to mix. Because each stereo frame has a left and right sample, a buffer with room for 200 samples requests 100 frames. There must be enough valid audio left in the block to fill that request.

If you provide a `VolumeSmoother`, the function uses it to adjust each track's volume as it mixes. Both channels receive the same adjustment, keeping the stereo balance unchanged. Without a smoother, every track stays at its original level.

The smoother must cover the same number of tracks as the block. Call its `begin_render()` before mixing so it can pick up the requested volume levels.

The function adds the tracks together using `double` values for extra precision, then stores the result as `float` samples. It waits until all tracks have been added before limiting the output to the range from `-1` to `1`. That way, tracks can balance each other out before anything is clipped.

If a combined sample falls outside that range, it is clipped to the nearest limit. For example, `1.2` becomes `1`. The function returns how many samples needed this adjustment. Left and right count separately, so clipping both channels in one frame adds two to the count. A sample already at either limit needs no adjustment and is not counted.

The original audio in the block stays unchanged, and the function uses the output buffer you provide without allocating more memory. It moves the smoother forward as it mixes, but leaves keeping track of which audio has been used to the caller.

## Example

```cpp
playset::StemBlock block(song.stems.size());
const auto frames = playset::decode_stem_block(song, block);

std::array<float, playset::playback_block_frames * playset::output_channels> output{};
const auto clipped = playset::mix_stem_block(
    block, 0, std::span(output).first(frames * playset::output_channels));
```

This reads one block and mixes its valid frames at their original levels. In normal playback, the queue separates those steps across the worker and audio threads.
