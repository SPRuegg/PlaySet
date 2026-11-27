# Audio

The audio module lets you inspect source files, read small chunks of decoded audio, and prepare a collection of stems. You can use it without creating a playback transport or opening an audio output device.

## Loading components

The declarations live in `src/audio/loading/stem_loader.hpp`:

| Component | What you can do |
| --- | --- |
| [StemInfo](loading/StemInfo.md) | Inspect a source file's location, format, sample rate, channel count, and frame count. |
| [StemReader](loading/StemReader.md) | Open one source file, read decoded samples, and choose where the next read begins. |
| [PreparedSong](loading/PreparedSong.md) | Keep the opened readers and shared information for a collection of stems together. |
| `prepare_stems(folder)` | Open the supported files directly inside a folder and check that their formats and sample rates match. |

The loader keeps the source sample rate and supports PCM WAV and AAC/ALAC M4A files. Each reader returns stereo samples, duplicating mono samples into both channels. Reading audio does not play it through your speakers.

## Using the module

Link your target to `PlaySet::Audio` and include the loading header:

```cmake
target_link_libraries(your_target PRIVATE PlaySet::Audio)
```

```cpp
#include "audio/loading/stem_loader.hpp"

auto song = playset::prepare_stems("/Tracks/Song/Stems");
```

`PlaySet::Playback` uses this module for source loading and adds buffering, mixing, transport, and audio output. The audio module itself does not depend on playback or the console. Its macOS decoder uses AudioToolbox and CoreFoundation, with platform details kept in `src/audio/loading/macos`.

The loading tests link directly to `PlaySet::Audio`, so they exercise the source readers without requiring playback components.

## Fingerprinting a source

Use [fingerprint_stem()](fingerprinting/StemFingerprint.md) when you want a versioned fingerprint for one source file. It samples short portions of WAV audio or encoded M4A packets with its own file resources, without starting playback or changing another reader's position.

For several sources, use [fingerprint_stems() and verify_stems()](fingerprinting/StemCollection.md). They identify the included stems together, keep individual source associations, and exclude named click and guide references from collection identity.
