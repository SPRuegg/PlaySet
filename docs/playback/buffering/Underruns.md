# Underruns

An underrun happens when the audio device requests more frames than the background reader has prepared. Playback cannot wait for the reader, so it outputs silence until enough audio is ready.

## Playback policy

While the background reader is still working, each callback receives either the full amount of requested audio or silence for the entire callback.

For example, if the device requests 512 frames but only 300 are ready:

| What happens | Result |
| --- | --- |
| Output | All 512 frames are silence. |
| Buffered audio | The 300 prepared frames stay in the queue. |
| Song position | Stays where it was. |
| Next callback | Checks again without waiting for the reader. |

Once enough frames are ready, playback resumes from the same source position. It does not skip audio to catch up with the time spent silent. All stems remain on the same timeline.

This keeps playback from sending small fragments simply because some audio is ready. You may still hear a gap if the reader falls behind.

## End of the song

Once the background reader has reached the end of the stems, a full callback is no longer required. Playback uses whatever audio remains and fills the unused part of the output with silence.

For example, if only 100 frames remain and the device requests 512, playback outputs those 100 frames followed by 412 frames of silence. The position advances by 100 frames.

If the background reader stops because of a decoding error, any audio it already prepared can still be used in the same way. Call `rethrow_if_failed()` on the control thread to check for that saved error.

## Queue behavior

Implemented in `src/playback/buffering/stem_block_queue.cpp`:

```cpp
RenderResult render(std::span<float> output, VolumeSmoother *volume = nullptr,
                    bool producer_finished = false) noexcept;
```

Before using any source frames, the audio thread checks which blocks are ready at that moment. Because the producer publishes each block only after filling it, playback can read those blocks without waiting or taking a lock.

If there are too few frames and `producer_finished` is `false`, the output stays silent and none of the queued audio is used. If the reader makes more audio available after that check, playback can use it on a later callback.

Pass `true` only once the background reader has finished publishing its blocks. This lets the queue play the remaining audio even when there is less than a full callback. `BufferedPlayback` handles this automatically.

Each callback supports up to 4,096 frames. The queue holds nine 512-frame blocks: eight blocks cover that maximum request, and the extra block leaves room when the first block has already been partly consumed. Without that extra space, the producer could fill every slot while playback still waits for a full callback. The native output reports an error if a callback exceeds the supported size.

While a shortage is producing silence, the queue does not move a volume ramp forward. Once source audio resumes, the ramp continues from where it stopped. Paused playback handles volume changes separately so they can take effect before resuming.

## Reporting

`BufferedPlayback::report()` includes `underruns`, the number of separate underrun episodes.

Several consecutive callbacks that cannot obtain enough audio count as one episode. Once audio resumes, another shortage starts a new episode. The normal partial callback at the end of a song does not count as an underrun.

`rendered_frames` increases only when audio is taken from the queue. The silence added during a shortage does not increase that count or move the song's position forward.
