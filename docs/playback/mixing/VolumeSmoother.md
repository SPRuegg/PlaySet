# VolumeSmoother

`VolumeSmoother` takes the requested levels from `MixerControls` and supplies volume multipliers for each track and the combined output, one audio frame at a time. It keeps changes gradual while playback is running.

## Definition

Public declarations from `src/playback/mixing/mixer_controls.hpp`:

```cpp
class VolumeSmoother {
public:
  VolumeSmoother(const MixerControls &volumes, double sample_rate,
                 VolumeSmoothing mode = VolumeSmoothing::exponential);
  void begin_render() noexcept;
  void settle() noexcept;
  std::span<const double> next_frame() noexcept;
  [[nodiscard]] double master_gain() const noexcept;
  // Private smoothing state omitted here.
};
```

Implemented in `src/playback/mixing/mixer_controls.cpp`.

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `VolumeSmoother(volumes, sample_rate, mode)` | Connects to the requested track levels and prepares the selected smoothing mode. |
| `begin_render()` | Picks up the latest requested levels before rendering a batch of audio. |
| `settle()` | Immediately adopts the latest requested levels, without a gradual transition. |
| `next_frame()` | Advances track and master smoothing by one frame and returns the track multipliers. |
| `master_gain()` | Reads the master multiplier for the current frame without advancing it. |

### Creating a smoother

The constructor takes a reference to `MixerControls`, rather than copying its levels into a separate set of controls. Later changes to those controls can therefore be picked up by the smoother.

The controls must remain alive for as long as the smoother uses them.

The sample rate determines how many frames make up the smoothing interval. It must be finite and between 8,000 and 192,000 Hz, inclusive. An invalid rate or an unrecognized smoothing mode throws `std::invalid_argument`.

The default mode is exponential. See [VolumeSmoothing](VolumeSmoothing.md) for how the two modes differ.

### Beginning a render

Call `begin_render()` before generating the first frame, and again at the start of each new batch of audio. It makes one attempt to read a complete, consistent set of requested levels. If a control update overlaps that read, it keeps the previous targets until a later call. An unchanged revision lets it skip reading the levels again.

On the first successful read, the smoother adopts those levels immediately. Until that read succeeds, it uses the original level of `1` for every track and the master. This ensures audio starts at the requested levels instead of fading in from zero.

On later calls, a changed target starts a gradual transition from the current level. Changes made after `begin_render()` are picked up on the next call, rather than halfway through the current batch.

If playback has too little buffered audio for a full callback, it outputs silence without calling `next_frame()`. The smoother can pick up new targets, but its frame-by-frame progress continues only when source audio resumes. See [Underruns](../buffering/Underruns.md).

A muted track uses a target of `0`, while its requested volume stays stored in `MixerControls`. When you unmute it, the smoother approaches that saved volume. If any tracks are soloed, unselected tracks also target zero. A muted track stays silent even when soloed. Track settings and master volume and mute are read together under the same revision check. The master has its own smoothing progress: changing it does not replace a track target or restart that track’s fade.

### Advancing one frame

```cpp
std::span<const double> next_frame() noexcept;
```

`next_frame()` returns one multiplier per track, in track-ID order. Apply each multiplier to both the left and right samples for that track. Call this once per audio frame, so both channels use the same level. That call also advances the master. After summing the adjusted tracks, multiply both channels by `master_gain()` before clipping. Reading `master_gain()` does not advance anything, so you can read it for both channels without moving through the fade twice.

A muted master targets `0` while its requested volume stays stored. Unmuting targets that stored volume. There is one master multiplier for the entire mix, including soloed tracks.

The returned span is a read-only view of the smoother's own array. It does not make a copy. Later calls update that same array, so use the values for the current frame before advancing again. The view must not be used after the smoother is destroyed.

### Applying levels immediately

`settle()` attempts to read the latest complete set of levels, then sets the current track and master levels to the last successfully accepted targets, clearing any remaining ramps. If an update overlaps, it settles to the previous targets rather than waiting. A completed update available before the call is picked up immediately.

This is useful while playback is silent. For example, a track turned down to zero while paused should already be silent when playback resumes. It should not briefly play at its old level while fading down.

The smoother does not decide whether playback is paused. The caller chooses when it is appropriate to settle.

## Private state and threading

The smoother keeps fixed-size arrays for the current levels, target levels, and progress of linear ramps. Each array has room for 256 tracks plus one master entry. `next_frame()` exposes only the track entries; `master_gain()` reads the master entry. Its render methods do not allocate memory or acquire locks.

One audio thread owns this progress. Calls to `begin_render()`, `settle()`, and `next_frame()` must not overlap across threads. The control thread updates `MixerControls` instead of changing the smoother directly.

All four render methods are marked `noexcept`; they do not throw exceptions.

## Example

```cpp
playset::MixerControls volumes(2);
volumes.set(0, 0.5);

playset::VolumeSmoother smoother(volumes, song.sample_rate);
smoother.begin_render();

const auto levels = smoother.next_frame();
// levels[0] is 0.5, and levels[1] is 1.0.
```

The first frame uses the requested levels immediately. After later volume changes, call `begin_render()` again to pick up the targets, then continue calling `next_frame()` as audio is rendered.
