# TransportStatus

`TransportStatus` describes whether playback is stopped, playing, paused, or at the end of the song.

## Definition

Declared in `src/playback/transport/transport.hpp`:

```cpp
enum class TransportStatus { stopped, playing, paused, ended };
```

## States

| State | Meaning |
| --- | --- |
| `stopped` | Playback is silent. Loading starts here, and a successful Stop returns here at position zero. |
| `playing` | Playback is running. A temporary underrun can produce silence without changing this status. |
| `paused` | Playback is silent and retains the current position. Play resumes from there. |
| `ended` | Playback has reached the song's end, or a playing seek moved directly to its endpoint. Play starts again from the beginning. |

If you seek while stopped, the status stays `stopped`, even when you choose a position other than zero. If you seek after the song ends, the status becomes `paused`, so choosing a new position does not unexpectedly start playback.

The status is reported by `Transport::snapshot()`. It reflects the transport state rather than whether the current samples happen to be silent or the tracks are turned down.

## Example

```cpp
const auto state = transport.snapshot();
if (state.status == playset::TransportStatus::paused) {
  transport.play();
}
```
