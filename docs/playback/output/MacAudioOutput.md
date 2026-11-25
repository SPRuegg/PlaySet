# MacAudioOutput

`MacAudioOutput` connects prepared audio to the default macOS output device. When macOS asks for another batch of samples, it fills the provided buffer using `BufferedPlayback`.

## Implementation

Defined privately in `src/playback/output/macos/audio_output.cpp`. You create it through [make_audio_output()](make_audio_output.md), which returns it as a `PlaybackOutput`.

The implementation uses Apple's AudioToolbox framework and a default-output Audio Unit. It keeps that unit alive through pauses, seeks, stops, and replays of the loaded song.

### Preparing the output

When you create the output, it finds the default-output component, creates an Audio Unit, and configures its input format:

| Setting | Value |
| --- | --- |
| Sample rate | The rate you supply, normally the loaded song's rate. |
| Channels | Two: left and right. |
| Sample type | 32-bit floating-point audio. |
| Layout | Alternating left and right samples in one buffer. |

For example, two stereo frames occupy four sample slots:

```text
left, right, left, right
```

This describes the audio supplied to the Audio Unit. The code does not configure the hardware's sample rate or promise that the device uses the same format internally.

The constructor installs the render callback, initializes the unit, and reads its reported latency. It prepares the connection without starting the callback clock yet.

### Shared output state

An internal `OutputState` holds the connection to `BufferedPlayback`, the play/pause command, the acknowledged command, the source-frame position, and the estimated end time.

Because commands, counters, and error codes use atomic values, your control thread and the audio callback can share them without making the callback wait on a lock. The callback also keeps track of whether it was playing on its previous call, allowing it to apply paused volume edits before audio resumes.

### Starting and pausing

When you call `play()`, the output publishes a playing command and starts the Audio Unit if it has not started already. Later resumes reuse that running unit.

When you call `pause()`, it publishes a paused command and waits for the callback to confirm it. Once paused, callbacks keep running but fill the buffer with silence. They do not consume source audio or advance your song position.

Because the clock stays alive, you can still seek while paused and have the position change acknowledged. A seek before the first Play starts that clock silently.

The command contains both a playing flag and a sequence number. That way, the control thread waits for confirmation of the particular command you requested, rather than mistaking an older pause for the new one.

### Filling a callback

At the beginning of each callback, the output checks whether a prepared seek bank is ready. If one is ready, it switches banks, updates the source position, clears the old end timing, and acknowledges the change.

It then checks whether playback should be running. While paused, or on the first callback after resuming, it settles the volume settings so your paused adjustments are already applied.

While playing, it asks `BufferedPlayback` to fill the output. If there is not enough prepared audio for a full callback, the buffer receives silence and your source position stays unchanged. The final partial buffer is still allowed to play. See [Underruns](../buffering/Underruns.md).

Each request must provide one writable stereo buffer with room for its requested frames. Requests above 4,096 frames or with an unexpected buffer layout are reported as an Audio Unit parameter error.

The callback does not open files, allocate sample buffers, wait for decoding, or throw exceptions. It records callback errors in an atomic value so your control thread can report them later.

### Position and the end of the song

Only source frames actually rendered increase the position. For example, rendering 512 frames advances it by 512; outputting a silent callback during an underrun adds nothing.

`finished()` checks both that the source queue has been exhausted and that the estimated end time of the last rendered audio has passed. To estimate that time, the callback uses its host timestamp, the duration of the source frames it rendered, and the Audio Unit's reported latency. If no valid host timestamp is available, it uses the current host clock.

This gives the last batch time to leave the output before normal completion is reported. Treat it as an estimate, rather than an exact measurement of sound leaving your speakers. It does not separately query the device's hardware latency or safety offset.

Pausing confirms that the callback is supplying silence. Audio already handed to the device may still take a short time to finish.

### Failures and cleanup

Setup failures throw an exception with the failed operation and AudioToolbox status code. `play()`, `seek()`, and `finished()` check for saved output or background decoding failures. While Pause waits for acknowledgement, it checks for those failures too.

Pause and seek acknowledgement waits have a ten-second timeout. Seek preparation can also wait for file access before its acknowledgement timeout starts. If a native seek fails, the output requests silence and saves the exception so later checked operations report the same failure.

The implementation does not currently install ongoing device-change or interruption listeners. The standalone playback helper can detect a lack of source progress, but an interactive transport does not have that same periodic progress watchdog built into the output.

An internal `UnitOwner` holds the Audio Unit. When you destroy the output, it stops the unit, uninitializes it, and disposes of it. Keep this cleanup on the control thread, with `BufferedPlayback` still alive until the output has finished shutting down.
