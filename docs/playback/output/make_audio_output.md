# make_audio_output()

`make_audio_output()` creates the native audio output for a buffered song. It gives you a `PlaybackOutput` interface while keeping the macOS-specific details private.

## Definition

Declared in `src/playback/output/audio_output.hpp`:

```cpp
[[nodiscard]] std::unique_ptr<PlaybackOutput>
make_audio_output(BufferedPlayback &playback, double sample_rate);
```

Implemented in `src/playback/output/macos/audio_output.cpp`.

## Parameters and return value

| Value | Purpose |
| --- | --- |
| `playback` | Supplies prepared audio and handles seek-bank preparation. |
| `sample_rate` | Configures the Audio Unit's input rate. Pass the loaded song's sample rate. |
| Return value | A `std::unique_ptr<PlaybackOutput>` that owns the native output. |

Because `playback` is passed by reference, the output stays connected to the same buffered song without copying or taking ownership of it. You must keep that song alive until after the output is destroyed.

The returned `std::unique_ptr` takes care of destroying the output when you release it. `[[nodiscard]]` asks the compiler to warn if you ignore that returned pointer.

### Creating without playing

The function creates and initializes the Audio Unit, but does not start audible playback. Call `play()` when you are ready to begin. A seek may start the callback clock silently so it can confirm your chosen position.

If setup fails, the function throws an exception. Any Audio Unit already created is cleaned up as the failed construction unwinds.

When you use `Transport`, this function is its default output factory. You usually do not need to call it yourself.

## Example

```cpp
auto song = playset::prepare_stems("/Tracks/Song/Stems");
const double sample_rate = song.sample_rate;
playset::BufferedPlayback playback(std::move(song));
playback.wait_until_ready();

auto output = playset::make_audio_output(playback, sample_rate);
```

Save the sample rate before moving `song` into buffered playback. The output then uses that rate and reads from the same `playback` instance.
