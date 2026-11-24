# BufferedPlayback

`BufferedPlayback` owns a prepared song and reads ahead on a background thread. When playback asks for audio, it uses those prepared samples, applies the track volumes, and mixes the tracks for output.

## Definition

Public declarations from `src/playback/buffering/buffered_playback.hpp`:

```cpp
class BufferedPlayback {
public:
  explicit BufferedPlayback(PreparedSong song, std::int64_t start_frame = 0,
                            std::shared_ptr<TrackVolumes> volumes = nullptr,
                            VolumeSmoothing smoothing = VolumeSmoothing::exponential);
  ~BufferedPlayback();
  void wait_until_ready();
  std::size_t render(std::span<float> output) noexcept;
  void settle_volumes() noexcept;
  [[nodiscard]] bool exhausted() const noexcept;
  [[nodiscard]] PlaybackReport report() const noexcept;
  void rethrow_if_failed() const;
  // Private worker, queue, and playback state omitted here.
};
```

Implemented in `src/playback/buffering/buffered_playback.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `BufferedPlayback(song, start_frame, volumes, smoothing)` | Takes ownership of the song, positions its readers, and starts reading ahead. |
| `~BufferedPlayback()` | Requests that the background thread stop, then waits for it to finish before releasing its resources. |
| `wait_until_ready()` | Waits for an initial supply of audio, completion, or a decoding failure. |
| `render(output)` | Fills an output buffer and returns the number of source frames consumed. |
| `settle_volumes()` | Applies accepted volume targets immediately instead of continuing a ramp. |
| `exhausted()` | Returns whether playback has confirmed that the background reader is finished and no queued audio remains. |
| `report()` | Returns the accumulated playback counters. |
| `rethrow_if_failed()` | Throws any decoding error saved by the background thread, so the calling code can handle it. |

### Preparing playback

Because `PreparedSong` owns readers that cannot be copied, you move it into `BufferedPlayback`. This hands over ownership of the readers so the background thread can use them.

`start_frame` tells each stem where to begin reading. Its default, `0`, starts at the beginning. Negative positions are rejected. If the position is past a stem's end, that reader moves to its end instead.

Because optional `TrackVolumes` are held through a `std::shared_ptr`, the controls stay alive while playback uses them. Their track count must match the song; a mismatch throws `std::invalid_argument`.

Without volume controls, each track uses its original level. With controls, the default smoothing mode is exponential; callers can choose linear at construction.

### Reading ahead

The background worker decodes each stem into its own track buffer, then publishes the completed block to the queue. Publishing makes the block available for playback. If the queue is full, the worker sleeps for 1 ms before checking again, keeping that wait off the audio thread.

`wait_until_ready()` gives playback a head start by waiting for three prepared blocks. If the worker finishes sooner, it stops waiting, so even a very short song can become ready. Call this on the control thread before starting audio output.

If decoding throws an exception, the worker saves it and stops. `wait_until_ready()` checks for a saved error before returning. To check for errors that happen later, call `rethrow_if_failed()` from the control thread and handle any exception it throws.

### Rendering and underruns

You give `render()` a buffer to fill with left and right samples. The buffer must hold complete stereo pairs, with room for at most 4,096 frames. Because it uses audio already prepared by the worker, the method does not read files, allocate memory, or wait for more audio.

Before mixing, it tries to pick up a complete set of requested volume levels. If the worker is still running but has not prepared enough audio to fill the output buffer, the whole buffer is filled with silence. The queued audio stays where it is, ready for the next attempt.

Once the worker finishes, playback uses whatever audio remains, even if it cannot fill the buffer. Any unused space is filled with silence. See [Underruns](Underruns.md) for examples.

Passing an empty buffer returns `0` without consuming audio or marking playback exhausted. After the last source frames have been consumed, the next nonempty render confirms that the finished worker has no audio left in the queue and marks playback exhausted. The audio device may still be playing samples it received earlier.

### Volumes while silent

Call `settle_volumes()` on the audio thread while output is silent or immediately before resuming. This lets paused volume changes take effect without briefly playing at the previous levels.

If a control update overlaps the read, the method uses the last complete set of targets accepted by the smoother. It does nothing when no volume controls were supplied.

### Reporting and cleanup

`report()` returns a [PlaybackReport](PlaybackReport.md) with totals collected by this playback instance. Reading it does not reset those totals. You can call `exhausted()` and `report()` without making the audio thread wait.

The destructor requests that the worker stop, then joins it, meaning it waits for the thread to finish before releasing the song and queue. Because a file operation may still be in progress, keep destruction outside the audio callback.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");
playset::BufferedPlayback playback(std::move(song));
playback.wait_until_ready();

std::array<float, playset::playback_block_frames * playset::output_channels> output{};
const auto frames = playback.render(output);
```

This prepares buffered playback and renders one batch into memory. Sending that buffer to an audio device is handled separately by the output component.
