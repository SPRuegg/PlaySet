# SongConfigurationCapture

`SongConfigurationCapture` lets you capture a loaded song's source references and requested mixer settings without changing playback. Keep one capture object for the configuration you are working on, and call `capture()` whenever you need its current values.

It produces a [SongConfiguration](SongConfiguration.md) in memory. It does not write a file or start playback.

## Definition

Declared in `src/songs/song_configuration_capture.hpp`:

```cpp
class SongConfigurationCapture {
public:
  SongConfigurationCapture(Transport &transport,
                           const std::filesystem::path &stem_folder);
  SongConfigurationCapture(const SongConfigurationCapture &) = delete;
  SongConfigurationCapture &operator=(const SongConfigurationCapture &) = delete;
  [[nodiscard]] SongConfiguration capture();
};
```

The implementation lives in `src/songs/song_configuration_capture.cpp`. Link your target to `PlaySet::SongCapture` to use it. That adapter connects the configuration format to playback; the `PlaySet::Songs` format target remains usable without a transport.

## Creating a capture object

```cpp
#include "songs/song_configuration_capture.hpp"
#include "playback/transport/transport.hpp"

playset::SongConfigurationCapture capture(transport, "/Tracks/Song/Stems");
const auto configuration = capture.capture();
const auto text = playset::serialize_song_configuration(configuration);
```

Pass your loaded transport and the folder its sources came from. The constructor resolves the folder to its canonical path and checks it against the transport's stored source paths. A different folder is rejected even if it contains copies of the same audio. A symbolic link to the correct folder is accepted after resolving it.

The source files must be direct children of that canonical folder. File links whose resolved targets live elsewhere are rejected because those targets cannot be represented as direct filenames within the chosen folder.

The constructor keeps a reference to your transport rather than taking ownership. Keep the transport alive for the capture object's lifetime. The capture object cannot be copied.

## Keeping identities stable

Creating the object assigns a random UUID to this song configuration. The macOS helper uses the system UUID functions. Each loaded track also receives an ID such as `track-0`, assigned once from its initial track ID.

Repeated captures keep those song and track IDs. Changing mixer settings, pausing, seeking, stopping, and replaying do not replace them. Each call returns a separate configuration, so later edits do not change a result you already received.

Creating another capture object starts a separate configuration with a new song ID. Keep your existing capture object when you want to update the same configuration. The persistent IDs belong to this object; they are not new fields or file-format responsibilities inside the transport.

## Capturing mixer settings

`capture()` first reads the requested track and master settings on the transport's control thread, before calculating fingerprints. It keeps volume, mute, and solo separately, including the stored volume of a muted track and every member of an additive solo selection. Click and guide references keep their settings too.

The result contains requested values rather than the temporary gains reached during smoothing. Capture does not pause, seek, rebuild the output, or move any playback reader. As with reading the transport's snapshot directly, it can recognize an already finished song as `ended` or report a background playback failure.

The source bindings stay tied to the loaded tracks. Adding another file to the folder does not add a track to the capture; load that file into a transport before treating it as part of a song.

## Fingerprints and failures

The first successful capture establishes the expected collection fingerprint. Each later call recalculates the fingerprints for those same loaded source paths and rejects a collection mismatch. It does not reuse a cached hash without checking the sources again.

Each included track receives its individual fingerprint by source path, so identical audio in separate files keeps separate track records and settings. Click and guide references follow the [collection exclusion rules](../audio/fingerprinting/StemCollection.md#reference-tracks): they carry `null` individual fingerprints and do not affect collection identity when their audio changes. Those excluded reference files are not revalidated by capture.

A missing, unreadable, invalid, or changing included source makes capture throw. A sampled mismatch also throws instead of silently adopting different musical stems as the same configuration. These checks have the same [sampling limits](../audio/fingerprinting/StemFingerprint.md#cost-and-limits) as the fingerprint API.

An unsuccessful capture returns no partial configuration, does not establish a new expected fingerprint, and does not change the song ID or mixer settings. You can fix the source problem and try the same capture object again.

## Calling safely and testing

Create and use the object on the transport's control thread. Fingerprinting performs blocking file access and uses its own readers, while the audio thread continues independently. Do not call it from an audio callback or from a second thread that could race with control changes.

`test/song_capture_test.cpp` uses silent test output. It checks live requested settings, additive solos, muted volumes, master controls, reference settings, duplicate stems, stable IDs, folder validation, failed-capture recovery, and that capture leaves playback running at its existing position. It never opens an audio device.
