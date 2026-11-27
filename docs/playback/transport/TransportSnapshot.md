# TransportSnapshot

`TransportSnapshot` describes playback at the moment you ask for its state. It stores the status, current position, total duration, and your requested master settings.

## Definition

Declared in `src/playback/transport/transport.hpp`:

```cpp
struct TransportSnapshot {
  TransportStatus status;
  double position_seconds;
  double duration_seconds;
  double master_volume = 1;
  bool master_muted = false;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `status` | `TransportStatus` | Whether playback is stopped, playing, paused, or ended. |
| `position_seconds` | `double` | Current source position, in seconds. |
| `duration_seconds` | `double` | Duration of the longest stem, in seconds. |
| `master_volume` | `double` | Requested master volume from `0` through `1`, even while muted. |
| `master_muted` | `bool` | Whether you have requested master mute. |

### Position

The output tracks the current position in source frames. `snapshot()` divides that count by the song's sample rate to report seconds.

For example, at 48,000 Hz, a position of 24,000 frames becomes `0.5` seconds. A seek resets the position to the chosen source frame, and rendered audio advances it from there. Silence inserted during an underrun does not advance it.

The position describes audio supplied to the output. It is not an exact measurement of what has reached the speakers, because the device can still have audio buffered. Normal end detection waits for the estimated output tail to finish before reporting `ended`.

### Reading a snapshot

Each time you call `Transport::snapshot()`, you receive a new copy of the state. A snapshot you already hold does not update automatically, so call the method again when you need the latest position or status.

The call can throw a saved decoding or output error, so handle those errors in the code requesting the snapshot. Once returned, the struct simply holds the values you received.

### Master settings

The master fields describe your requested controls. For example, after calling `set_master_volume(0.5)` and `set_master_muted(true)`, you receive `master_volume == 0.5` and `master_muted == true`. The stored volume remains available while the mix is silent.

A live change may still be smoothing toward its target when you read the snapshot. These fields do not measure the signal or tell you how far that fade has progressed. They stay unchanged through pauses, stops, seeks, and replay unless you change them yourself.

Editing your copy does not affect playback. Use `set_master_volume()` or `set_master_muted()` on the transport to request a change.

## Example

```cpp
const auto state = transport.snapshot();
const double position = state.position_seconds;
const double duration = state.duration_seconds;
```
