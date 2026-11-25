# Transport console

The transport console lets you try playback and mixer controls by typing commands. You can inspect track IDs, change volumes, mute selected tracks, and compare exclusive Solo with additive Soli.

## Opening the console

After building the app, run:

```sh
./build/PlaySet --transport "/Tracks/Song/Stems"
```

Replace the path with your stem folder. The song loads stopped, so you can adjust its settings before calling `play`. Audio uses your default output device when playback starts.

Enter `help` whenever you want to see the available commands. Enter one command per line; blank lines are ignored.

## Commands

| Command | What you can do |
| --- | --- |
| `tracks` | See each track's ID, name, requested volume, mute state, and solo state. |
| `volume <id> <0..1>` | Set one track's requested volume. |
| `mute <id> <on\|off>` | Mute or unmute one track. |
| `mutes <id>... <on\|off>` | Mute or unmute several selected tracks together. |
| `solo <id> <on\|off>` | Enable exclusive Solo, or disable that track's solo. |
| `soli <id>... <on\|off>` | Add tracks to the solo selection, or remove the named tracks. |
| `play` | Start or resume playback. At the end, start again from the beginning. |
| `pause` | Silence playback while keeping your position. |
| `stop` | Silence playback and return to the beginning. |
| `seek <seconds>` | Move to a position without changing whether playback should be audible. |
| `status` | See the current status, position, and duration. |
| `help` | Show the command list again. |
| `quit` | Leave the console and release the loaded song and output. |

Use lowercase command names and `on` or `off` for mute and solo states. These commands set a state explicitly rather than toggling whatever state happens to be active.

### Finding your tracks

Start with `tracks`. For example, you might see:

```text
0: Bass volume=1 mute=off solo=off
1: Drums volume=1 mute=off solo=off
2: Piano volume=1 mute=off solo=off
```

Use those IDs in later commands. They start at `0` and stay the same through pauses, stops, seeks, and replay. Your folder's filenames determine the names and loaded order, so check the listing instead of assuming which ID belongs to an instrument.

The flags describe your requested settings. A live change may still be smoothing toward its target. A track with both mute and solo enabled remains silent once that mute has settled.

### Changing volume and mute

```text
volume 0 0.5
mute 0 on
volume 0 0.25
mute 0 off
```

Here, you lower track `0`, mute it, and adjust its stored volume while it stays muted. Unmuting uses the latest requested value, `0.25`.

To mute tracks `0` and `2` together:

```text
mutes 0 2 on
```

Pass `off` to unmute that same selection. Other tracks keep their existing mute settings. The selection is published as one batch, so playback does not adopt only part of it.

### Solo and Soli

```text
solo 0 on
solo 1 on
```

The first command selects track `0`. The second replaces that selection with track `1`.

If you want to keep track `1` selected and add tracks `0` and `2`, use:

```text
soli 0 2 on
```

Pass `off` to remove only the tracks you name. For example, `solo 1 off` removes track `1` while leaving the other solo selections in place. Once you remove the last solo, all unmuted tracks can play normally again.

Mute keeps priority over both Solo and Soli. Soloing a track does not unmute it or change its volume.

### Checking paused edits

```text
pause
mutes 0 1 on
tracks
status
```

Your position should stay unchanged. When you resume with `play`, those tracks should already be muted. Live changes use the selected smoothing mode; paused changes settle before audio resumes.

## Invalid input

If you enter an invalid command, the console prints an error and lets you try again. Missing values, unexpected extra arguments, invalid IDs, and states other than `on` or `off` are rejected.

Batch commands accept 1 through 256 IDs. For example, if track `99` does not exist, `mutes 0 99 on` rejects the entire selection without muting track `0`. The same rule applies to Soli. Repeated IDs have the same effect as listing each track once.

Volume must be between `0` and `1`, and seek positions must be within the song. You can enter `tracks` or `status` after an error to check that your settings stayed unchanged.

## Testing

The console tests send commands through standard input and check the displayed track settings, transport state, and errors. They use silent output, so you can run them without opening an audio device.
