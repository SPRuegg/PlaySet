# Transport

`Transport` controls when a song plays and where playback begins. It keeps all stems on the same timeline while preserving track and master settings through pauses, stops, seeks, and replays.

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
  void set_track_solo(TrackId id, bool enabled);
  void set_tracks_soli(std::span<const TrackId> ids, bool enabled);
  void set_master_volume(double volume);
  void set_master_muted(bool muted);
  [[nodiscard]] std::vector<TrackSnapshot> tracks() const;
  [[nodiscard]] std::span<const StemInfo> stems() const noexcept;
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
| `set_track_solo(id, enabled)` | Enables an exclusive solo, or disables solo for that track. |
| `set_tracks_soli(ids, enabled)` | Adds or removes selected tracks without clearing other solos. |
| `set_master_volume(volume)` | Sets the combined output's requested volume from `0` through `1`. |
| `set_master_muted(muted)` | Mutes or unmutes the whole mix without changing its stored volume. |
| `tracks()` | Returns track IDs, names, requested volumes, and mute and solo settings. |
| `stems()` | Gives you read-only source information in loaded track order. |
| `snapshot()` | Returns the current status, position, duration, and requested master settings. |

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

Call `set_track_solo(id, true)` to hear just one track, replacing any previous solo selection. To add several tracks without clearing existing solos, pass their IDs to `set_tracks_soli(ids, true)`. Passing `false` removes only the tracks you name. Removing the last solo restores normal playback.

Muted tracks stay silent even when soloed. Solo changes preserve requested volumes and mute flags, use the selected smoothing mode while playing, and settle before resuming from pause. Both solo APIs keep their selections through Stop, Seek, and replay. `tracks()` reports each track's requested `solo` state.

`tracks()` returns copies of the current track information. `snapshot()` also checks for background failures and recognizes when the output has finished playing the song's final audio. It can therefore update the status to `ended`.

Use `stems()` when you need the source paths and audio attributes behind those tracks. Its entries follow track ID order, so entry `0` belongs to track `0`. You receive a read-only view of the stored [StemInfo](../../audio/loading/StemInfo.md) values, without reopening files or moving a reader. Keep the transport alive while using that view. The information describes the sources when loaded; it does not recheck files on disk.

### Master volume and mute

Each newly created transport starts with master volume `1` and master mute off. Call `set_master_volume(0.5)` to halve the combined signal's amplitude, or pass `0` for silence. The master affects every track, including soloed tracks, and is applied before final output clipping.

Call `set_master_muted(true)` to silence the whole mix. This keeps your requested master volume and individual track settings. You can change the master volume while muted; passing `false` to unmute uses that latest value.

For example:

```cpp
transport.pause();
transport.set_master_muted(true);
transport.set_master_volume(0.25);
transport.play();
```

Here, playback resumes silently. If you pause, unmute, and play again, the first audio uses master volume `0.25` immediately. If you unmute while playing instead, the output gradually approaches that level using the selected smoothing mode.

Master settings stay with this transport through Pause, Stop, Seek, and replay. They do not reset the song position, reopen the stems, recreate the output, or alter any track's volume, mute, or solo setting. A separate transport starts with its own defaults.

Read `snapshot().master_volume` and `snapshot().master_muted` to see your requested settings. They describe your controls, rather than the current signal level or progress through a fade.

Master volume must be finite and between `0` and `1`, inclusive. Other values throw `std::invalid_argument` without changing the settings or playback position.

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
