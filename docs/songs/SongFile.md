# Saving a song configuration

`save_song_configuration()` writes a [SongConfiguration](SongConfiguration.md) to a `.plsong` file. You can use the same function for a new destination or to update a file you already saved.

## Definition

Declared in `src/songs/song_file.hpp`:

```cpp
void save_song_configuration(const std::filesystem::path &file,
                              const SongConfiguration &song);
```

Link your target to `PlaySet::Songs`. The public implementation lives in `src/songs/song_file.cpp`; macOS temporary-file and replacement operations stay in `src/songs/macos/atomic_song_file.cpp`.

## Saving

```cpp
#include "songs/song_file.hpp"

playset::save_song_configuration("/Documents/Songs/Example Song.plsong", configuration);
```

Pass the destination and a complete configuration. The function validates and serializes the configuration before touching the destination folder. On success, the file contains the same JSON returned by `serialize_song_configuration()`, including its final newline.

You can pass an absolute or relative path. The filename must end in `.plsong`; extension checks ignore letter case. The parent folder must already exist. This function does not create folders or choose a default location.

To capture current playback settings first, combine it with [SongConfigurationCapture](SongConfigurationCapture.md):

```cpp
const auto configuration = capture.capture();
playset::save_song_configuration(destination, configuration);
```

Saving uses the configuration you pass. It does not change the song ID, mixer settings, playback position, or audio files. It also does not recalculate fingerprints or require that the referenced source folder currently exists. Capture and source verification remain separate operations.

## Protecting an existing file

The function creates a uniquely named `.playset-song-XXXXXX.tmp` file beside your destination. It writes the complete JSON there, handles partial writes, flushes the temporary file with `fsync`, and closes it before replacing the destination.

Replacement uses a single rename within that same folder. Until that rename succeeds, the existing destination stays untouched. Readers opening the destination see the old complete file or the new complete file, rather than partially written JSON. Readers that already have the old file open can continue reading that original file.

The successful rename is the commit point. There are no further filesystem operations that can report a failed save after the destination has been replaced.

If validation, temporary-file creation, writing, flushing, closing, or replacement fails, the function throws and leaves the existing destination unchanged. If you were creating a new destination, a failed save does not leave a partially written file under that destination name. Cleanup attempts to remove only this call's temporary file.

An existing destination must be a regular file. Destination symlinks, including dangling links, and directories are rejected. A symlink in the parent folder path is resolved before creating the temporary file.

New files are created with owner read/write permissions (`0600`). Updating an existing file preserves its ordinary user, group, and other permission bits. Replacement creates a new file, so ownership, ACLs, extended attributes, and hard-link relationships are not copied from the old file.

## Interrupted saves and limits

An ordinary save failure cleans up its temporary file when possible. A crash before replacement can leave that temporary file behind. The function does not scan, recover, or delete temporary files from earlier attempts; unrelated temporary files are left alone.

The temporary file's contents are flushed before replacement. The final directory update is not separately flushed, so sudden power loss can still affect which directory entry survives. Atomic replacement prevents partial destination writes; it does not provide a session journal or coordinate competing editors.

Run saving outside the audio callback. It performs blocking file access, and your configuration must remain unchanged for the duration of the call. The function returns normally on success and throws on validation or filesystem failure.

## Tests

`test/song_file_test.cpp` checks new saves, overwrites, exact JSON contents, basic permissions, destination validation, parent-folder aliases, and that destination symlinks are preserved when rejected. It also checks that a save does not remove an unrelated temporary file.

The failure tests limit a child process to writing 128 bytes per file. That causes a real partial write followed by a filesystem error. They verify that an existing destination keeps its original contents, a new destination is not created, and the failed calls leave no temporary files behind. The limit and signal handling apply only to that child process. An unwritable-folder check also runs when the tests are not running as root.
