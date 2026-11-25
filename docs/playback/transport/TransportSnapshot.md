# TransportSnapshot

`TransportSnapshot` describes playback at the moment you ask for its state. It stores the status, current position, and total duration.

## Definition

Declared in `src/playback/transport/transport.hpp`:

```cpp
struct TransportSnapshot {
  TransportStatus status;
  double position_seconds;
  double duration_seconds;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `status` | `TransportStatus` | Whether playback is stopped, playing, paused, or ended. |
| `position_seconds` | `double` | Current source position, in seconds. |
| `duration_seconds` | `double` | Duration of the longest stem, in seconds. |

### Position

The output tracks the current position in source frames. `snapshot()` divides that count by the song's sample rate to report seconds.

For example, at 48,000 Hz, a position of 24,000 frames becomes `0.5` seconds. A seek resets the position to the chosen source frame, and rendered audio advances it from there. Silence inserted during an underrun does not advance it.

The position describes audio supplied to the output. It is not an exact measurement of what has reached the speakers, because the device can still have audio buffered. Normal end detection waits for the estimated output tail to finish before reporting `ended`.

### Reading a snapshot

Each time you call `Transport::snapshot()`, you receive a new copy of the state. A snapshot you already hold does not update automatically, so call the method again when you need the latest position or status.

The call can throw a saved decoding or output error, so handle those errors in the code requesting the snapshot. Once returned, the struct simply holds the values you received.

## Example

```cpp
const auto state = transport.snapshot();
const double position = state.position_seconds;
const double duration = state.duration_seconds;
```
