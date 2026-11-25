# play_song()

`play_song()` plays a prepared song from beginning to end and returns its playback counters. You can use it when you want a simple blocking playback operation instead of controlling a transport interactively.

## Definition

Declared in `src/playback/output/audio_output.hpp`:

```cpp
[[nodiscard]] PlaybackReport play_song(PreparedSong song);
```

Implemented in `src/playback/output/macos/audio_output.cpp`.

### Taking ownership of the song

Because `PreparedSong` owns readers that cannot be copied, you move the song into this function. It then owns those readers for the duration of playback.

The function creates a `BufferedPlayback`, waits for its initial audio to be ready, creates the native output, and starts playing. It uses the song's existing sample rate.

### Waiting for completion

The call stays active until the output reports that it has finished. It checks progress every 2 ms on the calling thread, while the worker prepares audio and the audio callback supplies it to the device independently.

If the source position stops changing for more than ten seconds while the song is not finished, the function throws `std::runtime_error`. Setup errors, callback errors, and saved decoding failures can also throw.

The progress check measures source frames, not the presence of audible sound. A song containing silence can still make progress normally. A prolonged underrun does not advance the position and can trigger the timeout.

Keep this function off the audio callback and any UI thread you need to keep responsive. If you need Play, Pause, Stop, Seek, or live volume controls, use [Transport](../transport/Transport.md).

### Returning the report

After normal completion, you receive a [PlaybackReport](../buffering/PlaybackReport.md) containing rendered-frame, clipped-sample, and underrun totals. `[[nodiscard]]` asks the compiler to warn if you ignore that result.

The output is destroyed before buffered playback leaves the function. That stops callbacks before the worker, queues, and source readers are released. Cleanup also runs if an exception is thrown.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");
const auto report = playset::play_song(std::move(song));
const auto underruns = report.underruns;
```

This starts audible playback and returns after the song finishes, or throws if playback fails.
