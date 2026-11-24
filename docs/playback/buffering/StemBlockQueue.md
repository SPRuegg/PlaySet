# StemBlockQueue

`StemBlockQueue` passes prepared audio from a background reader to the audio thread. The reader fills unused blocks while playback consumes blocks that are ready, keeping file access away from the audio callback.

## Definition

Public declarations from `src/playback/buffering/stem_block_queue.hpp`:

```cpp
class StemBlockQueue {
public:
  static constexpr std::size_t maximum_render_frames = 4096;
  static constexpr std::size_t capacity =
      maximum_render_frames / playback_block_frames + 1;
  explicit StemBlockQueue(std::size_t track_count);
  StemBlock *begin_write() noexcept;
  void commit_write() noexcept;
  RenderResult render(std::span<float> output, VolumeSmoother *volume = nullptr,
                      bool producer_finished = false) noexcept;
  [[nodiscard]] std::size_t ready_blocks() const noexcept;
  // Private queue storage and positions omitted here.
};
```

Implemented in `src/playback/buffering/stem_block_queue.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `StemBlockQueue(track_count)` | Allocates the blocks needed to hold audio for every track. |
| `begin_write()` | Returns the next free block, or `nullptr` if the queue is full. |
| `commit_write()` | Publishes the filled block so playback can read it. |
| `render(output, volume, producer_finished)` | Mixes prepared audio into the output buffer and returns a `RenderResult`. |
| `ready_blocks()` | Returns the number of published blocks not yet fully consumed. |

### Filling a block

The background reader, called the producer here, calls `begin_write()` to get a free block. It fills that block, sets `frames` to a value from 1 through 512, and calls `commit_write()` to make the audio available for playback.

If the queue is full, `begin_write()` returns immediately with `nullptr`. The producer can try again later; unread audio is never overwritten.

Until you call `commit_write()`, only the producer uses that block. After committing it, playback may read it, so stop changing its samples. You can write to that storage again when `begin_write()` returns it as a free block.

### Rendering audio

You supply a stereo output buffer with alternating left and right samples. Its sample count must be even, with space for at most 4,096 frames. Debug builds check these requirements with assertions; the method does not throw an exception for an invalid buffer.

While the producer is still working, `render()` consumes audio only when enough frames are ready to fill the entire output buffer. Otherwise, it fills the output with silence and leaves the queue's read position unchanged. See [Underruns](Underruns.md).

When `producer_finished` is `true`, the queue can render fewer frames and pad the rest with silence. The caller must only pass `true` once the producer has finished publishing its final blocks.

One callback can use part of a block or read across several blocks. Because the queue remembers where the previous read stopped, any unused audio stays ready for the next callback. An empty output buffer consumes nothing and returns zero counts.

If you supply a `VolumeSmoother`, the queue uses it to adjust the track volumes while mixing. It must cover the same number of tracks, and you must call its `begin_render()` first. Both channels use the same adjustment for each frame. Without a smoother, tracks play at their original levels.

### Capacity

The queue holds nine blocks of 512 frames each. Eight full blocks would cover the largest supported request of 4,096 frames. But if playback has already used part of the first block, those eight blocks contain less than 4,096 unread frames. The ninth block gives the producer room to make up that difference.

Because all blocks are allocated when the queue is created, playback does not need to allocate more memory as it runs. The samples take 36 KiB per track, or about 9 MiB for 256 tracks, plus a small amount for the queue's other fields.

`ready_blocks()` counts blocks that still have unread audio. For example, a block with just one frame left still counts as one ready block, so the result does not tell you exactly how many frames are available.

## Thread ownership

Keep `begin_write()` and `commit_write()` on one producer thread, and `render()` on one audio thread. Call `ready_blocks()` from the audio thread, or from the control thread before output starts.

Because the read and write positions are stored in atomic values, the producer can make blocks available while playback reads them, without making playback wait on a lock. Rendering does not allocate memory, decode files, or wait for more audio.

## Example

```cpp
playset::StemBlockQueue queue(1);
if (auto *block = queue.begin_write()) {
  auto samples = block->track(0);
  samples[0] = 0.25F;
  samples[1] = -0.25F;
  block->frames = 1;
  queue.commit_write();
}

std::array<float, 2> output{};
const auto result = queue.render(output);
```

Here the queue has exactly the one frame requested, so it consumes that frame and returns `result.frames == 1`.
