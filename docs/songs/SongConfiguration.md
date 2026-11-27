# Song configuration

`SongConfiguration` describes the source stems and mixer settings you want to keep for one song. A `.plsong` document stores those values as JSON, while the audio stays in its original folder.

You can turn a configuration into JSON with `serialize_song_configuration()`. This function does not read audio, write a file, or change playback.

## Using the module

Link your target to `PlaySet::Songs` and include `songs/song_configuration.hpp`:

```cpp
const std::string text = playset::serialize_song_configuration(configuration);
```

You supply a complete `SongConfiguration`. The function validates its fields and returns JSON with two-space indentation and a final newline. Calling it again with the same configuration returns the same text. If validation fails, it throws `std::invalid_argument` instead of returning part of a document.

The module uses the audio module's fingerprint types. It does not depend on playback or the console. Its JSON library stays behind this interface, so your calling code works with ordinary C++ structs and strings.

## What you store

Declared in `src/songs/song_configuration.hpp`:

| C++ field | JSON field | What it keeps |
| --- | --- | --- |
| `SongConfiguration::schema_version` | `schemaVersion` | The document format version, currently `1`. |
| `SongConfiguration::id` | `id` | A stable identifier for this song configuration. |
| `SongConfiguration::stem_folder` | `stemFolder` | The absolute path to the original source folder. |
| `SongConfiguration::fingerprint` | `fingerprint` | The collection version, included stem count, and digest. |
| `SongConfiguration::tracks` | `tracks` | The source association and requested settings for every track, including references. |
| `SongConfiguration::master` | `master` | The requested volume and mute state for the whole mix. |

The song ID identifies the configuration; the collection fingerprint identifies its sampled musical stems. Two configurations can use the same audio while keeping different IDs and settings.

You supply the IDs yourself. Serialization does not generate or replace them. Keep a song's ID stable when saving it again, and assign a different ID when creating a separate configuration.

There is no song name, key, tempo, playback position, or arrangement in this format. The document holds source references and mixer configuration.

## Track identity and settings

Each `SongTrackConfiguration` contains:

| C++ field | JSON field | What it keeps |
| --- | --- | --- |
| `id` | `id` | A stable identifier within this song configuration. |
| `file` | `file` | The source filename inside `stemFolder`, including its extension. |
| `fingerprint` | `fingerprint` | The individual fingerprint, or `null` for an excluded reference. |
| `volume` | `volume` | The requested multiplier, from `0` through `1`. Defaults to `1`. |
| `muted` | `mute` | Whether this track is muted. Defaults to `false`. |
| `solo` | `solo` | Whether this track belongs to the solo selection. Defaults to `false`. |

A track ID is not its position in the array. For example, `track-7` stays `track-7` when you move it before `track-0`, rename its source, or remove another track. Serialization keeps the IDs and array order you supply.

Two separate files can have the same individual fingerprint. Keep their track IDs and filenames distinct so their settings remain associated with the intended source. A fingerprint alone cannot distinguish identical audio copies.

Volume, mute, and solo are stored independently. A volume of `0` does not turn on mute, and a muted track can still belong to the solo selection. Several tracks can have `solo: true` to preserve an additive selection. These are requested settings rather than the temporary levels reached while smoothing.

`SongMasterConfiguration` keeps its own `volume` and `muted` fields, written as `master.volume` and `master.mute`. Their defaults are `1` and `false`.

## Reference tracks and fingerprints

Click and guide references remain in `tracks` with their filenames, IDs, and mixer settings. Their individual `fingerprint` is `null`, because they did not contribute to the collection identity. Musical stems carry their individual version and digest.

Use the [collection's reference policy](../audio/fingerprinting/StemCollection.md#reference-tracks) when preparing that association. The serializer does not classify filenames or calculate fingerprints. It checks that the collection's `stemCount` equals the number of tracks carrying individual fingerprints, with at least one included stem.

Both collection and individual fingerprints store their version explicitly. Digests are written as 64 lowercase hexadecimal characters. Their [sampling limits](../audio/fingerprinting/StemFingerprint.md#cost-and-limits) still apply.

## JSON shape

This example shows one musical stem and one guide. The digest values illustrate their shape; an actual captured configuration uses the calculated fingerprints.

```json
{
  "schemaVersion": 1,
  "id": "eb277073-0546-48df-9040-cf4af075df63",
  "stemFolder": "/Music/Example Song/MultiTracks",
  "fingerprint": {
    "version": 1,
    "stemCount": 1,
    "digest": "efefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefef"
  },
  "tracks": [
    {
      "id": "track-0",
      "file": "Bass.wav",
      "fingerprint": {
        "version": 1,
        "digest": "abababababababababababababababababababababababababababababababab"
      },
      "volume": 0.5,
      "mute": false,
      "solo": true
    },
    {
      "id": "track-9",
      "file": "Guide.wav",
      "fingerprint": null,
      "volume": 1.0,
      "mute": false,
      "solo": false
    }
  ],
  "master": {
    "volume": 1.0,
    "mute": false
  }
}
```

All fields shown are emitted, including default settings and `null` reference fingerprints. The schema version is separate from the fingerprint versions: changing the document layout and changing the sampling policy are different changes.

## Validation and source access

You can serialize between 1 and 256 tracks, counting references. Track IDs and filenames must each be unique within the document. Song and track IDs contain 1–128 ASCII letters, digits, hyphens, or underscores; UUID text and IDs such as `track-0` fit those rules.

The stem folder must be an absolute path. Each track file must be a single WAV or M4A filename, with no parent path. Extensions ignore letter case. Paths cannot contain null characters, and their text must be valid UTF-8 when encoded as JSON.

Track and master volumes must be finite values between `0` and `1`, inclusive. NaN and infinity are rejected rather than silently becoming JSON `null` values. This initial implementation accepts schema version `1` and the current individual and collection fingerprint versions.

Serialization does not check whether the folder or files exist, verify the audio, or confirm that the collection digest agrees with its individual digests. It also does not require matching formats or sample rates. Those checks belong to source preparation and verification; this function works with the configuration you supply.

## Build dependency and tests

The module uses [JSON for Modern C++](https://json.nlohmann.me/integration/cmake/), pinned to version `3.12.0` and a SHA-256 archive checksum. CMake downloads it into the build directory when needed. Its sources and license stay with that downloaded dependency.

`test/song_configuration_test.cpp` checks JSON fields and escaping, decimal precision, independent mixer settings, reference records, stable IDs, duplicate audio, version rejection, invalid paths and volumes, and track-count limits. These tests construct their configurations in memory and do not require source audio or a running transport.
