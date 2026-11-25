# TrackVolumes

`TrackVolumes` stores the requested volume for each track. Every track starts at its original level and can be turned down independently, all the way to silence.

## Definition

Declared in `src/playback/mixing/track_volume.hpp`:

```cpp
class TrackVolumes {
public:
  explicit TrackVolumes(std::size_t count);
  void set(TrackId id, double volume);
  void set_all(std::span<const double> volumes);
  [[nodiscard]] double get(TrackId id) const;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
  friend class VolumeSmoother;
  std::size_t count_;
  // One control-thread publisher; odd revisions mark an unfinished update.
  std::atomic<std::uint64_t> revision_{0};
  std::array<std::atomic<double>, maximum_tracks> targets_{};
};
```

Implemented in `src/playback/mixing/track_volume.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `TrackVolumes(count)` | Creates volume controls for a fixed number of tracks, each starting at `1`. |
| `set(id, volume)` | Changes one track's requested volume. |
| `set_all(volumes)` | Changes every track's requested volume as one batch, in track-ID order. |
| `get(id)` | Returns one track's requested volume. |
| `size()` | Returns the number of tracks without changing anything or throwing an exception. |

### Track IDs

The same header defines the type and maximum track count:

```cpp
using TrackId = std::size_t;
inline constexpr std::size_t maximum_tracks = 256;
```

A track ID is its index in this collection, starting at `0`. For three tracks, the valid IDs are `0`, `1`, and `2`.

These are positions within the loaded song, rather than identifiers saved permanently with an audio file. `TrackVolumes` does not assign names or discover tracks; the caller keeps each ID matched to the correct track.

### Volume values

Volume is a multiplier applied to the audio samples:

| Value | Effect |
| --- | --- |
| `0` | Silence. |
| `0.5` | Half the original sample amplitude, approximately −6.02 dB. |
| `1` | The original level, also called unity gain. |

Half the amplitude does not necessarily sound half as loud. These values describe the change applied to the audio, rather than perceived loudness.

`get()` returns the requested level. While a change is being smoothed, the level currently applied to playback can still be approaching that value.

## Validation

The constructor accepts between 1 and 256 tracks. That count stays fixed for the lifetime of the object.

`set()` accepts finite values from `0` through `1`, inclusive. Values outside that range, NaN, and infinity are rejected with `std::invalid_argument`. The previous value stays unchanged.

`set_all()` requires exactly one value per track. It validates the entire batch before publishing, so an invalid value or count throws `std::invalid_argument` without changing any levels.

An unknown track ID passed to `set()` or `get()` throws `std::out_of_range`. An invalid constructor count throws `std::invalid_argument`.

## Sharing levels with playback

Because each requested level is stored in an atomic value, you can change a track’s requested level while playback reads it, without making playback wait.

A revision counter marks each update as in progress, then complete. The smoother adopts the values only if the revision stays unchanged throughout its read. If an update overlaps, it keeps the previous targets and tries again on a later render call. It never waits or retries in a loop. The implementation checks that both the level and revision atomics are lock-free.

Use `set_all()` when several levels should change together. Separate `set()` calls are separate updates, so playback may adopt one before the next is made. `get()` reads only one track; it does not provide a snapshot of the whole collection.

Keep all `set()` and `set_all()` calls on one control thread. This publication scheme supports one writer while playback reads concurrently.

`friend class VolumeSmoother` lets the smoother read the stored targets directly. Other callers use the public methods.

## Example

```cpp
playset::TrackVolumes volumes(3);

const std::array<double, 3> levels{0.5, 0, 1};
volumes.set_all(levels);

const double first_track = volumes.get(0); // 0.5
const double third_track = volumes.get(2); // 1.0
const std::size_t track_count = volumes.size(); // 3
```

This stores the requested levels. Applying them gradually is handled by `VolumeSmoother`.
