# RenderResult

`RenderResult` describes the audio produced by one call to the queue's `render()`. It stores how many source frames were used and how many output samples exceeded the allowed amplitude before being clipped.

## Definition

Declared in `src/playback/buffering/stem_block_queue.hpp`:

```cpp
struct RenderResult {
  std::size_t frames = 0;
  std::uint64_t clipped_samples = 0;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `frames` | `std::size_t` | Number of source audio frames consumed during this render. |
| `clipped_samples` | `std::uint64_t` | Number of output samples limited to `-1` or `1` because the mixed value went beyond that range. |

### Frames and samples

A stereo frame contains two samples: left and right. If both channels exceed the allowed amplitude in one frame, `clipped_samples` increases by two.

`frames` counts only audio taken from the queue. For example, if the last 100 frames of a song are followed by 412 frames of added silence, the result is `100`, not `512`.

During an underrun, both fields are `0`: the callback receives silence, and the queued source audio stays untouched. At the end of the song, `frames` can be smaller than the requested amount because the remaining audio is allowed to play.

## Example

```cpp
playset::RenderResult result{512, 2};
```

Here, the render used 512 source frames and clipped two individual output samples. Those samples could belong to the same stereo frame or to two different frames.
