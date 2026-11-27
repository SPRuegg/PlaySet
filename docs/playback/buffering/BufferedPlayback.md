# BufferedPlayback

`BufferedPlayback` owns a prepared song and reads ahead on a background thread. When playback asks for audio, it uses those prepared samples, applies the track volumes, and mixes the tracks for output.

## Definition

Public declarations from `src/playback/buffering/buffered_playback.hpp`:

```cpp
class BufferedPlayback {
public:
  explicit BufferedPlayback(PreparedSong song, std::int64_t start_frame = 0,
                            std::shared_ptr<MixerControls> volumes = nullptr,
                            VolumeSmoothing smoothing = VolumeSmoothing::exponential);
  ~BufferedPlayback();
  void wait_until_ready();
  void prepare_seek(std::int64_t frame);
  [[nodiscard]] bool seek_pending() const noexcept;
  std::optional<std::int64_t> apply_pending_seek() noexcept;
  void acknowledge_seek() noexcept;
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
| `prepare_seek(frame)` | Asks the worker to prepare another source position in the inactive bank. |
| `seek_pending()` | Reports whether a requested bank switch still needs acknowledgement. |
| `apply_pending_seek()` | Switches banks at the start of an audio callback and returns the new source position. |
| `acknowledge_seek()` | Confirms the switch after the output updates its position and timing. |
| `render(output)` | Fills an output buffer and returns the number of source frames consumed. |
| `settle_volumes()` | Applies accepted volume targets immediately instead of continuing a ramp. |
| `exhausted()` | Returns whether playback has confirmed that the background reader is finished and no queued audio remains. |
| `report()` | Returns the accumulated playback counters. |
| `rethrow_if_failed()` | Throws any decoding error saved by the background thread, so the calling code can handle it. |

### Preparing playback

Because `PreparedSong` owns readers that cannot be copied, you move it into `BufferedPlayback`. This hands over ownership of the readers so the background thread can use them.

`start_frame` tells each stem where to begin reading. Its default, `0`, starts at the beginning. Negative positions are rejected. If the position is past a stem's end, that reader moves to its end instead.

Because optional `MixerControls` are held through a `std::shared_ptr`, the controls stay alive while playback uses them. Their track count must match the song; a mismatch throws `std::invalid_argument`.

Without volume controls, each track uses its original level. With controls, the default smoothing mode is exponential; callers can choose linear at construction.

### Reading ahead

The background worker decodes each stem into its own track buffer, then publishes the completed block to the active queue. Publishing makes the block available for playback. If the queue is full, the worker sleeps for 1 ms before checking again, keeping that wait off the audio thread.

`wait_until_ready()` gives playback a head start by waiting for three prepared blocks. If the worker reaches the end sooner, it stops waiting, so even a very short song can become ready. Call this on the control thread before starting audio output.

If decoding throws an exception, the worker saves it and stops. `wait_until_ready()` checks for a saved error before returning. To check for errors that happen later, call `rethrow_if_failed()` from the control thread and handle any exception it throws.

### Rendering and underruns

You give `render()` a buffer to fill with left and right samples. The buffer must hold complete stereo pairs, with room for at most 4,096 frames. Because it uses audio already prepared by the worker, the method does not read files, allocate memory, or wait for more audio.

Before mixing, it tries to pick up a complete set of requested volume levels. If the worker is still running but has not prepared enough audio to fill the output buffer, the whole buffer is filled with silence. The queued audio stays where it is, ready for the next attempt.

Once the worker reaches the end of the stems, playback uses whatever audio remains, even if it cannot fill the buffer. Any unused space is filled with silence. See [Underruns](Underruns.md) for examples.

Passing an empty buffer returns `0` without consuming audio or marking playback exhausted. After the last source frames have been consumed, the next nonempty render confirms that the worker has reached the end and no audio remains in the active queue and marks playback exhausted. The audio device may still be playing samples it received earlier.

### Preparing another position

Two queues act as audio banks. A seek prepares the inactive one while the current bank can continue supplying audio. The original stem readers stay open, and the same worker repositions them to prepare the destination.

`prepare_seek()` belongs to the control thread. It waits for preparation, but does not perform the switch itself. The audio thread calls `apply_pending_seek()` at a callback boundary, updates the output position, then calls `acknowledge_seek()`. Only after that acknowledgement may another seek be requested.

The worker stays alive after reaching the end of the song so it can handle future seeks. The smoother and volume controls also stay alive across bank switches. See [Seeking](../transport/Seeking.md) for the full handoff and error behavior.

Each queue's sample storage is 36 KiB per track. With two banks, the instance uses 72 KiB per track for queued samples, plus decoder buffers and other fields. Neither bank holds the entire song.

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

This prepares buffered playback and renders one batch into memory. Sending that buffer to an audio device is handled separately by [PlaybackOutput](../output/PlaybackOutput.md).
