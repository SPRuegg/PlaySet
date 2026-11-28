# Song saving

Use `save-as` and `save` in the [transport console](Transport.md) to keep the loaded song's source references and current mixer settings in a `.plsong` file.

## Your first save

After starting the console with your stem folder, enter:

```text
volume 0 0.5
master-volume 0.75
save-as "Example Song.plsong"
```

The console captures your current settings, calculates the stem fingerprints, and writes the configuration. A successful save prints `Saved` followed by the absolute destination path. The source folder comes from the transport's loaded source paths; you do not choose the stems again when saving.

You can use an absolute path or a path relative to the console's working directory. Include the `.plsong` extension, and create the parent folder beforehand. Paths containing spaces need double quotes. Inside quotes, use `\"` for a literal double quote and `\\` for a literal backslash.

## Updating the file

After your first successful Save As, enter:

```text
mute 0 on
volume 0 0.25
save
```

This updates the remembered file with your latest requested settings. The stored volume stays `0.25` even though the track is muted. Track volume, mute, solo, master volume, and master mute are all captured independently.

`save` takes no arguments. If you have not successfully chosen a destination yet, it asks you to use `save-as` first without writing anything.

## Choosing another destination

```text
save-as "Another Mix.plsong"
save
```

The successful Save As changes the destination used by later `save` commands. It keeps the same song and track IDs because you are writing the current configuration to another path. The earlier file stays as it was unless you choose that path again.

Both commands replace an existing regular file at the chosen destination. They use the [safe saving API](../songs/SongFile.md), which writes a unique temporary file before replacing the destination.

If Save As fails, the previous successful destination stays selected. A failed first Save As leaves no destination selected. Errors go to the error stream, and the console keeps accepting commands.

## Source checks and playback

The console creates one [SongConfigurationCapture](../songs/SongConfigurationCapture.md) when saving is first requested and reuses it for later saves. The first successful capture establishes its expected source fingerprint. Later saves recalculate that fingerprint and reject a mismatch instead of silently accepting different musical stems. Choosing another destination does not reset that source check.

Click and guide references keep their mixer settings but stay excluded from collection identity. Identical musical stems remain separate tracks with separate IDs and settings.

You can save while stopped, paused, or playing. The commands do not pause playback, seek, or change mixer settings. Fingerprinting and writing can take time on the console's control thread while audio rendering continues independently. Saved volumes are your requested settings, rather than the current level of a smoothing transition.

The remembered destination and capture object belong to the current console invocation. Leaving the console releases that state; starting another console session begins without a selected destination.

## Testing

`test/song_save_console_test.py` sends commands to the real console using silent test output. It checks saved settings, reference records, duplicate audio, stable IDs, Save As destination changes, failed saves, quoted and relative paths, source mismatches, and preservation of stopped, paused, and playing states. These tests write only inside their temporary folders and never open an audio device.
