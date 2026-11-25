# Transport

`Transport` controls when a song plays and where playback begins. It keeps all stems on the same timeline while preserving track volumes through pauses, stops, seeks, and replays.

## Definition

Public declarations from `src/playback/transport/transport.hpp`:

```cpp
class Transport {
public:
  using OutputFactory = std::function<std::unique_ptr<PlaybackOutput>(
      BufferedPlayback &, double)>;
  explicit Transport(PreparedSong song, OutputFactory output = make_audio_output,
                     VolumeSmoothing smoothing = VolumeSmoothing::exponential);
  ~Transport();
  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;

  void play();
  void pause();
  void stop();
  void seek(double seconds);
  void set_track_volume(TrackId id, double volume);
  void set_track_muted(TrackId id, bool muted);
  void set_tracks_muted(std::span<const TrackId> ids, bool muted);
  [[nodiscard]] std::vector<TrackSnapshot> tracks() const;
  [[nodiscard]] TransportSnapshot snapshot();
  // Private song, buffering, output, and state fields omitted here.
};
```

Implemented in `src/playback/transport/transport.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `Transport(song, output, smoothing)` | Takes ownership of a prepared song, buffers its beginning, and creates its audio output. |
| `play()` | Starts or resumes playback. At the song's end, prepares the beginning and plays again. |
| `pause()` | Silences playback and retains its position. |
| `stop()` | Silences playback and prepares the beginning, leaving the position at zero. |
| `seek(seconds)` | Moves every stem to the requested time while preserving whether playback should be audible. |
| `set_track_volume(id, volume)` | Sets one track's requested volume from `0` through `1`. |
| `set_track_muted(id, muted)` | Mutes or unmutes one track while preserving its requested volume. |
| `set_tracks_muted(ids, muted)` | Mutes or unmutes your selected tracks as one coherent update. |
| `tracks()` | Returns track IDs, names, and requested volumes. |
| `snapshot()` | Returns the current status, position, and duration. |

### Creating a transport

Move a `PreparedSong` into the constructor to hand over its readers. The transport initially stays stopped at the beginning; constructing it does not start audible playback.

By default, [make_audio_output()](../output/make_audio_output.md) creates the native macOS output. You can supply another factory, such as a test output that renders into memory. Your factory receives the buffered playback instance and the song's sample rate, and returns a [PlaybackOutput](../output/PlaybackOutput.md).

Only one output is created for the loaded transport. Seeking, stopping, and replaying reuse it, along with the original stem readers. Track IDs and volume controls stay the same throughout those operations.

### Play, Pause, and Stop

Call `play()` to start from the current position. If you are already playing, it does nothing. If you have reached the end, it prepares the beginning before playing again.

When you call `pause()`, it waits for the output to confirm a silent callback before returning. Your paused position then stays where it is. Calling it while stopped, paused, or ended leaves the state unchanged.

Call `stop()` to pause the output and return to the beginning. It prepares audio at position zero and waits for the output to confirm the change. When it returns successfully, your transport is `stopped` at zero. Stop may take longer than Pause because the background reader needs to reposition and prepare audio.

### Seeking

You pass `seek()` a time in seconds. It rounds that time to the nearest source frame, so every stem moves to the same frame boundary. Negative times, non-finite values, and times beyond the longest stem are rejected with `std::invalid_argument` before changing playback.

A seek while playing continues playing from the new position. A seek while paused or stopped stays silent. Seeking after playback has ended leaves the transport paused at the requested position. Seeking to the exact end while playing changes the status to `ended`.

The worker prepares the destination in an inactive audio bank. The audio callback switches banks only when that preparation is complete. See [Seeking](Seeking.md) for how this avoids rebuilding the output.

### Volumes and snapshots

`set_track_volume()` changes the requested level, not the source file. Playback smooths live changes using the mode selected at construction. The default is exponential; linear is also available. Paused edits are applied before playback resumes.

Call `set_track_muted(id, true)` to mute a track and pass `false` to unmute it. The track's requested volume stays unchanged. You can adjust that volume while muted; unmuting uses the updated setting. Live changes are smoothed, while paused changes settle before audio resumes.

To change several tracks together, pass their IDs and a mute state to `set_tracks_muted()`. Only those tracks change, and the whole selection is published together. An invalid ID rejects the entire request.

Mute state stays with the loaded track through Pause, Stop, Seek, and replay. `tracks()` includes that requested state as `muted`.

`tracks()` returns copies of the current track information. `snapshot()` also checks for background failures and recognizes when the output has finished playing the song's final audio. It can therefore update the status to `ended`.

## Thread ownership and errors

Keep all transport methods on one control thread. Commands are synchronous, meaning the call waits for its work to finish before returning. A seek may wait for file access and a callback acknowledgement, but that wait stays on your control thread. The audio thread continues independently.

A decoding failure is saved and rethrown to the control thread. A native seek failure or missing acknowledgement silences the output and makes subsequent output operations report the saved failure. Reload the song to create a new transport after such a failure.

The transport cannot be copied. Its output is destroyed before its buffered playback, ensuring callbacks have stopped before the worker and readers are released. Keep destruction outside the audio callback.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");
playset::Transport transport(std::move(song));

transport.play();
transport.pause();
transport.seek(0);
transport.set_track_volume(0, 0.5);
transport.play();
```

Here, seeking back to the beginning keeps playback paused. The final `play()` resumes with the first track at half its original sample amplitude.
