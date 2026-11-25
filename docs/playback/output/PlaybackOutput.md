# PlaybackOutput

`PlaybackOutput` gives you a common way to start, pause, and reposition audio output. `Transport` uses this interface to control playback without depending on the details of the macOS audio device.

## Definition

Declared in `src/playback/output/audio_output.hpp`:

```cpp
class PlaybackOutput {
public:
  virtual ~PlaybackOutput() = default;
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void seek(std::int64_t frame) = 0;
  [[nodiscard]] virtual std::int64_t position_frames() const = 0;
  [[nodiscard]] virtual bool finished() = 0;
};
```

## Public declarations

| Declaration | Purpose |
| --- | --- |
| `~PlaybackOutput()` | Lets the implementation stop its callbacks and release its output resources when you destroy it. |
| `play()` | Starts or resumes output from its current position. |
| `pause()` | Silences future output while retaining the position. |
| `seek(frame)` | Prepares and switches to a new source frame, keeping playback audible or silent as it was before. |
| `position_frames()` | Returns the current source position in audio frames. |
| `finished()` | Checks whether the output has finished the current audio and reports any relevant failure. |

### An interface rather than a device

The `= 0` declarations make these methods pure virtual. You cannot create a `PlaybackOutput` directly; you use an implementation that provides those methods.

For normal playback, [make_audio_output()](make_audio_output.md) creates the native macOS implementation. In a test, you can supply another implementation that writes samples into memory instead of sending them to speakers.

Because the destructor is virtual, destroying a `std::unique_ptr<PlaybackOutput>` also runs the implementation's cleanup.

### Position and seeking

`seek()` takes a source frame number, while `Transport::seek()` takes seconds. For example, in a 48,000 Hz song, frame `48000` is one second from the beginning.

The native output waits for a prepared bank to be applied at a callback boundary before returning from a seek. If you seek while paused, the position stays at the chosen frame. If you seek while playing, callbacks can already have advanced beyond it by the time you read the position.

`position_frames()` describes source audio supplied for output. The device may still have some of that audio buffered, so this is not an exact measurement of what you have already heard.

### Thread ownership and lifetime

Keep these methods on one control thread. The output's audio callback runs independently. Do not call methods that wait, such as `pause()` or `seek()`, from that callback.

The buffered playback you give the output must stay alive until after the output is destroyed. Destruction stops the callbacks before returning, so you can then release their audio source safely.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");
const double sample_rate = song.sample_rate;
playset::BufferedPlayback playback(std::move(song));
playback.wait_until_ready();

auto output = playset::make_audio_output(playback, sample_rate);
output->play();
output->pause();
```

Here, `output` is destroyed before `playback` because it was declared afterward. That keeps the buffered audio alive while the output cleans up its callbacks.
