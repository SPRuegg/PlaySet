# VolumeSmoothing

`VolumeSmoothing` chooses how a track's volume approaches a new level. Gradual changes help avoid clicks caused by abrupt changes to the audio samples.

## Definition

Declared in `src/playback/mixing/track_volume.hpp`:

```cpp
enum class VolumeSmoothing { exponential, linear };
```

## Modes

| Mode | Behavior |
| --- | --- |
| `exponential` | Moves quickly at first, then approaches the requested level more gradually. This is the default. |
| `linear` | Changes by an equal amount each frame until it reaches the requested level. |

### Exponential smoothing

Exponential smoothing uses a **5 ms time constant**. That means about 63% of the change is completed after 5 ms, rather than the entire change finishing in that time.

For example, when moving from `0` to `1`, the level is approximately `0.632` after 5 ms and `0.950` after 15 ms. It then continues approaching `1`.

Once the remaining difference is at most `0.000000001`, the smoother sets the value exactly to the target. This allows a target of `0` to reach exact silence.

### Linear smoothing

Linear smoothing completes the change in approximately **5 ms**, rounded to a whole number of audio frames. At 48,000 Hz, that takes 240 frames.

For example, moving from `0` to `1` reaches approximately `0.5` halfway through the ramp and exactly `1` at the end.

### Changing the target again

Both modes continue from the level already reached when a new target is picked up. They do not jump back to the beginning of the previous change. A linear ramp takes another approximately 5 ms to reach its new target.

The mode is selected when constructing a `VolumeSmoother`. Its current interface does not change the mode afterward.

## Example

```cpp
// Using exponential (default)
playset::TrackVolumes volumes(2);

// Using linear
playset::VolumeSmoother default_smoothing(volumes, song.sample_rate);
playset::VolumeSmoother linear_smoothing(
    volumes, song.sample_rate, playset::VolumeSmoothing::linear);
```
