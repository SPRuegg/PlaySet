# TrackSnapshot

`TrackSnapshot` describes one loaded track's ID, name, and requested volume. `Transport::tracks()` returns these values for every track in the song.

## Definition

Declared in `src/playback/transport/transport.hpp`:

```cpp
struct TrackSnapshot {
  TrackId id;
  std::string name;
  double volume;
  bool muted = false;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `id` | `TrackId` | Track index used when requesting volume changes. |
| `name` | `std::string` | Source filename without its final extension. |
| `muted` | `bool` | Whether you have requested that the track be muted. |
| `volume` | `double` | Requested volume multiplier from `0` through `1`. |

For example, a source file named `Bass.wav` produces the name `Bass`. IDs begin at `0` and follow the loaded stem order. Pausing, stopping, seeking, and replaying do not change them.

The volume is the requested setting. While a live change is being smoothed, the level currently applied to the audio may still be approaching that value. `0` requests silence, and `1` keeps the original level.

Because you receive copies of the track information, editing a snapshot does not change playback. To adjust a track, pass its ID and your requested volume to `set_track_volume()`.

`muted` reports your requested mute state. A live mute can still be fading to silence when you read it. The `volume` field keeps its requested value even while muted, so unmuting restores that level.

## Example

```cpp
const auto tracks = transport.tracks();
transport.set_track_volume(tracks.front().id, 0.5);
```

The first track is set to half its original sample amplitude. Request a new track snapshot to see its updated setting.
