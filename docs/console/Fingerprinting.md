# Fingerprint inspection

You can inspect a folder's stem fingerprints without starting playback or opening an audio output device:

```sh
./build/PlaySet --fingerprint "/Tracks/Song/Stems"
```

Replace the path with the folder containing your WAV or M4A files. The command checks that folder directly; it does not search its subfolders. Extensions ignore letter case, and unrelated files and directories are skipped.

## Reading the results

The command shows the collection version and digest, how many stems were included, how many named reference tracks were excluded, and the individual fingerprint version. Each remaining line pairs an individual digest with its filename.

You can compare the collection digest across runs, as long as the versions match. Moving the folder, renaming musical stems, or changing their order leaves that digest unchanged. The displayed source records follow filename order, so their order can change when you rename files.

Click and guide references follow the [collection exclusion rules](../audio/fingerprinting/StemCollection.md#reference-tracks). They stay on disk and are not opened by this command. Adding another musical stem changes the collection, even when it is a copy of an existing stem.

Fingerprint inspection identifies the supplied audio rather than checking whether the folder can play as one song. You can inspect a mixture of WAV and M4A files or different sample rates here. The playback loader still requires one format category and one sample rate throughout a song.

## Failures and limits

If an included source is missing, unreadable, corrupt, or changes during the observed checks, the command reports an error and exits with status `1`. It calculates the complete result before printing anything, so you do not receive a partial fingerprint listing. Successful inspection exits with status `0`.

You can inspect up to 256 supported files, counting references. An empty folder or a folder containing only excluded references is rejected.

This is a sampled identity check. Edits outside the sampled windows can be missed, and silent samples can match across different recordings. See [StemFingerprint](../audio/fingerprinting/StemFingerprint.md) for the sampling policy and its limits.

The command reads sources without changing them. It creates no cache or other files. Its runtime depends on the number of stems, their formats, and your storage device.

## Timing examples

These local measurements used a Release build on an Apple M5 on November 27, 2026. Each collection was inspected six times in separate processes. Times cover launching the command, discovering and fingerprinting the files, and capturing the complete listing.

| Collection | Format | Files / included stems | Source size | First observed run | Median of next five |
| --- | --- | --- | --- | --- | --- |
| WASHED | WAV | 43 / 41 | 1,623.7 MiB | 251.7 ms | 34.0 ms |
| No One Like The Lord (Live) | M4A | 22 / 20 | 421.7 MiB | 98.2 ms | 75.7 ms |

Each collection excluded two references and returned the same fingerprint in all six runs. Storage caches were not cleared, so the first run is not a guaranteed cold-cache measurement. These timings describe fingerprint inspection; they do not measure how long playback takes to load a song.

## Calling from code

Declared in `src/console/fingerprint_console.hpp`:

```cpp
void inspect_stem_fingerprints(const std::filesystem::path &folder,
                               std::ostream &output);
```

Pass your source folder and the stream where you want the listing. The function discovers the supported files, sorts their paths, calculates the collection, and prints the completed result. It throws on failure; the startup command catches that exception and reports it to standard error.

You can use a string stream to capture the listing without writing to the terminal. Keep the call outside the audio callback because it performs blocking file access.

## Tests

`test/fingerprint_console_test.py` checks the command's arguments, versions, counts, source associations, reference exclusions, duplicate audio, repeated M4A inspection, and failure output. It also checks that inspection does not enter transport startup. The command tests use silent output and never start playback.
