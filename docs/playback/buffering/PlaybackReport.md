# PlaybackReport

`PlaybackReport` contains the playback counters collected by one `BufferedPlayback` instance. It lets other code inspect progress and buffering problems without changing playback.

## Definition

Declared in `src/playback/buffering/buffered_playback.hpp`:

```cpp
struct PlaybackReport {
  std::uint64_t rendered_frames = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t underruns = 0;
};
```

## Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `rendered_frames` | `std::uint64_t` | Total source frames consumed by the instance. |
| `clipped_samples` | `std::uint64_t` | Total individual output samples that exceeded the allowed amplitude before clipping. |
| `underruns` | `std::uint64_t` | Number of separate episodes where playback lacked a full callback of prepared audio. |

### Reading the counters

`rendered_frames` counts audio taken from the queue and mixed for output. Silence added during a shortage does not increase it. If you start halfway through a song, the count still begins at zero. It tells you how much source audio this instance has rendered, rather than how long playback has been running or what has already reached the speakers.

Clipping is counted separately for the left and right output samples. If both clip in one frame, the count increases by two.

If several callbacks in a row cannot get enough audio, they count as one underrun episode. Once audio resumes, a later shortage starts another episode. The normal partial callback at the end of a song does not count as an underrun.

Because each counter is stored in an atomic value, you can read the report while playback updates it, without making playback wait. The counters are read separately, so their values may come from slightly different moments. Keep that in mind when comparing them.

These totals belong to the current `BufferedPlayback` instance. Creating a replacement instance starts new counters.

Pausing, stopping, and seeking within the same instance do not reset these totals. For example, seeking backward and playing a section again adds its frames to `rendered_frames` again, so that count is not the current song position.

## Example

```cpp
const auto report = playback.report();
const auto consumed = report.rendered_frames;
const auto shortages = report.underruns;
```

Reading the report does not reset its counters.
