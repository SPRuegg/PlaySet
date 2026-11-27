# MixerControls

`MixerControls` stores your requested track settings and the master volume and mute state. You can adjust individual tracks or turn the whole mix down without losing those settings.

## Definition

Declared in `src/playback/mixing/mixer_controls.hpp`:

```cpp
class MixerControls {
public:
  explicit MixerControls(std::size_t count);
  void set(TrackId id, double volume);
  void set_all(std::span<const double> volumes);
  void set_muted(TrackId id, bool muted);
  void set_muted(std::span<const TrackId> ids, bool muted);
  void set_solo(TrackId id, bool enabled);
  void set_soli(std::span<const TrackId> ids, bool enabled);
  void set_master_volume(double volume);
  void set_master_muted(bool muted);
  [[nodiscard]] double master_volume() const noexcept;
  [[nodiscard]] bool master_muted() const noexcept;
  [[nodiscard]] double get(TrackId id) const;
  [[nodiscard]] bool is_muted(TrackId id) const;
  [[nodiscard]] bool is_solo(TrackId id) const;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
  friend class VolumeSmoother;
  void change_solo(std::span<const TrackId> ids, bool enabled, bool exclusive);
  std::size_t count_;
  // One control-thread publisher; odd revisions mark an unfinished update.
  std::atomic<std::uint64_t> revision_{0};
  std::array<std::atomic<double>, maximum_tracks> targets_{};
  std::array<std::atomic<bool>, maximum_tracks> muted_{};
  std::array<std::atomic<bool>, maximum_tracks> solo_{};
  std::atomic<double> master_volume_{1};
  std::atomic<bool> master_muted_{false};
};
```

Implemented in `src/playback/mixing/mixer_controls.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `MixerControls(count)` | Creates volume controls for a fixed number of tracks, each starting at `1`. |
| `set(id, volume)` | Changes one track's requested volume. |
| `set_all(volumes)` | Changes every track's requested volume as one batch, in track-ID order. |
| `set_muted(id, muted)` | Mutes or unmutes a track without changing its requested volume. |
| `set_muted(ids, muted)` | Mutes or unmutes your selected tracks together as one batch. |
| `set_solo(id, enabled)` | Enables an exclusive solo, or disables solo for that track. |
| `set_soli(ids, enabled)` | Adds or removes your selected tracks from the solo selection. |
| `is_solo(id)` | Returns whether the track is selected for solo. |
| `is_muted(id)` | Returns whether the track is muted. |
| `get(id)` | Returns one track's requested volume. |
| `set_master_volume(volume)` | Changes the requested level of the combined output. |
| `set_master_muted(muted)` | Mutes or unmutes the whole mix without changing its requested volume. |
| `master_volume()` | Returns your requested master volume, even while muted. |
| `master_muted()` | Returns whether you have requested master mute. |
| `size()` | Returns the number of tracks without changing anything or throwing an exception. |

### Track IDs

The same header defines the type and maximum track count:

```cpp
using TrackId = std::size_t;
inline constexpr std::size_t maximum_tracks = 256;
```

A track ID is its index in this collection, starting at `0`. For three tracks, the valid IDs are `0`, `1`, and `2`.

These are positions within the loaded song, rather than identifiers saved permanently with an audio file. `MixerControls` does not assign names or discover tracks; the caller keeps each ID matched to the correct track.

### Volume values

Volume is a multiplier applied to the audio samples:

| Value | Effect |
| --- | --- |
| `0` | Silence. |
| `0.5` | Half the original sample amplitude, approximately −6.02 dB. |
| `1` | The original level, also called unity gain. |

Half the amplitude does not necessarily sound half as loud. These values describe the change applied to the audio, rather than perceived loudness.

`get()` returns the requested level. While a change is being smoothed, the level currently applied to playback can still be approaching that value.

### Muting a track

Call `set_muted(id, true)` to silence a track, or `set_muted(id, false)` to let it play again. Tracks start unmuted.

Because mute is stored separately from volume, `get()` still returns the requested level while a track is muted. You can adjust that level with `set()` or `set_all()` without making the track audible. When you unmute it, playback uses the latest level you requested.

While playing, mute and unmute changes use the selected smoothing mode to avoid an abrupt jump. A live mute therefore takes a short time to reach exact silence. While paused, the output settles the change before playback resumes.

You can also pass a list of track IDs to `set_muted()` to update several tracks together. For example:

```cpp
const std::array<playset::TrackId, 2> selected{0, 2};
volumes.set_muted(selected, true);
```

This mutes tracks `0` and `2` together while leaving track `1` unchanged. Pass `false` to unmute the same selection. Playback adopts the complete batch or keeps its previous targets; it never adopts just part of this update.

Every ID is checked before any mute flags change. If one is invalid, the whole request throws `std::out_of_range` without changing any tracks. An empty selection does nothing, and repeated IDs have the same effect as listing each track once. These are selections you supply, rather than stored groups or buses.

### Solo and Soli

Tracks start with solo disabled. When no tracks are soloed, every unmuted track can play at its requested volume.

Call `set_solo(id, true)` when you want to hear just one track. Enabling it clears the other solo selections. For example, soloing track `2` replaces an earlier solo on track `0`.

To hear several tracks together, pass their IDs to `set_soli(ids, true)`. This adds those tracks to the existing selection without clearing earlier solos. You can call it again to add more tracks.

Pass `false` to either method to remove only the tracks you name. Disabling one solo does not clear the others. Once you remove the last solo, normal playback returns.

Mute still takes priority. If you solo a muted track, it stays silent. It still counts as selected for solo, so an exclusively soloed, muted track leaves the whole mix silent. Your requested volumes and mute flags stay unchanged through solo changes.

Like mute, live solo changes use the selected smoothing mode. Paused edits settle before audio resumes. `is_solo()` reports your selection, rather than whether a track is currently audible.

`set_soli()` accepts 1 through 256 IDs. Empty or oversized selections throw `std::invalid_argument`. An invalid ID throws `std::out_of_range` before any solo flags change. Repeated IDs have the same effect as listing each track once.

### Master controls

The master starts at `1` with mute off. Call `set_master_volume(0.5)` to halve the amplitude of the combined signal, or pass `0` for silence. Playback applies this adjustment after combining the tracks and before clipping the final output. Every track is affected, including soloed tracks.

Call `set_master_muted(true)` to silence the whole mix without changing your requested master volume. You can still adjust that volume while muted. Passing `false` unmutes the mix at the latest requested level.

For example, with a `MixerControls` instance named `volumes`:

```cpp
volumes.set_master_volume(0.5);
volumes.set_master_muted(true);
volumes.set_master_volume(0.25);
volumes.set_master_muted(false);
```

Here, unmuting uses `0.25`. Individual track volumes, mute flags, and solo selections stay as you left them. Live changes follow the selected smoothing mode; the audio consumer can call `settle()` on its smoother while silent to apply edits immediately.

`master_volume()` and `master_muted()` report your requested settings, rather than how far playback has progressed through a fade. Each getter reads one value, so separate calls do not give you a grouped snapshot.

## Validation

The constructor accepts between 1 and 256 tracks. That count stays fixed for the lifetime of the object.

`set()` and `set_master_volume()` accept finite values from `0` through `1`, inclusive. Values outside that range, NaN, and infinity are rejected with `std::invalid_argument`. The previous value stays unchanged.

`set_all()` requires exactly one value per track. It validates the entire batch before publishing, so an invalid value or count throws `std::invalid_argument` without changing any levels.

An unknown track ID passed to `set()`, `get()`, `set_muted()`, `is_muted()`, `set_solo()`, or `is_solo()` throws `std::out_of_range`. An invalid constructor count throws `std::invalid_argument`.

## Sharing levels with playback

Because each requested level is stored in an atomic value, you can change a track’s requested level while playback reads it, without making playback wait.

A revision counter marks each update as in progress, then complete. The smoother adopts the values only if the revision stays unchanged throughout its read. If an update overlaps, it keeps the previous targets and tries again on a later render call. It never waits or retries in a loop. The implementation checks that both the level and revision atomics are lock-free.

Use `set_all()` when several levels should change together. Separate `set()` calls are separate updates, so playback may adopt one before the next is made. `get()` reads only one track; it does not provide a snapshot of the whole collection.

Mute flags are read within the same revision check as the volumes. Playback accepts a complete set of effective levels, with muted tracks targeting zero. Changing volume does not clear mute.

Solo flags are checked within the same revision as mute and volume. If any track is soloed, unselected tracks target zero. An exclusive solo switch clears the old selection and enables the new one in a single publication, so playback never adopts half of that change.

Master volume and mute are checked within that same revision, so playback adopts the track and master targets from one consistent read. Each setter is a separate update; calling several setters does not group them into one transaction.

Keep all track and master setters on one control thread. This publication scheme supports one writer while playback reads concurrently.

`friend class VolumeSmoother` lets the smoother read the stored targets directly. Other callers use the public methods.

## Example

```cpp
playset::MixerControls volumes(3);

const std::array<double, 3> levels{0.5, 0, 1};
volumes.set_all(levels);

const double first_track = volumes.get(0); // 0.5
const double third_track = volumes.get(2); // 1.0
const std::size_t track_count = volumes.size(); // 3
```

This stores the requested levels. Applying them gradually is handled by `VolumeSmoother`.
