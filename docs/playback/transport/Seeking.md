# Seeking

When you seek, every stem moves to the same new position while the audio output stays alive. Playback first prepares another bank of audio, then switches to it at the beginning of an audio callback.

## Two audio banks

`BufferedPlayback` owns two `StemBlockQueue` banks. One supplies the current audio. The other stays available for preparing the next position.

When you request a seek:

1. The control thread asks the background worker to prepare the requested frame.
2. The worker clears the inactive queue and seeks the existing stem readers. It fills at least three blocks, or reaches the end if less audio remains.
3. Once that audio is ready, the worker publishes the prepared bank. Until then, the callback continues using the old bank.
4. At the beginning of a callback, the output switches to the prepared bank and updates the source position. It clears the previous end-of-audio timing and resets playback's exhausted state.
5. The callback acknowledges the switch. The control thread can then return, and the worker continues filling the new active bank.

Because the worker fills the destination before publishing it, the audio thread never has to seek a file or wait for decoding. It also never mixes part of the old bank with part of the new one in the same callback.

While the worker prepares a seek, the old bank can continue playing the audio it already holds. If preparation takes long enough to empty that bank, the usual underrun policy produces silence until the switch is ready.

## Position and playback state

The callback updates the position before acknowledging the switch. If you seek while paused or stopped, `seek()` returns with the position at your chosen frame. If you seek while playing, playback may already have moved a little farther by the time the call returns.

The callback clock keeps running while paused, allowing it to acknowledge position changes even when you are not playing audio. If you seek before the first Play, the native output starts that clock silently. You still need to call Play to hear the song.

Stop uses the same mechanism to prepare position zero. Play after the song ends uses it to prepare the beginning before replaying. All of these operations reuse one audio output and the same open stem readers.

## Preparing and acknowledging a bank

These `BufferedPlayback` methods separate the two threads' responsibilities:

| Method | Called by | Purpose |
| --- | --- | --- |
| `prepare_seek(frame)` | Control thread | Requests preparation and waits until the worker has a bank ready. |
| `seek_pending()` | Control thread | Checks whether the requested switch still needs an acknowledgement. |
| `apply_pending_seek()` | Audio thread | Switches to a ready bank and returns its source position, or no value if nothing is ready. |
| `acknowledge_seek()` | Audio thread | Confirms the switch after the output has updated its position and timing. |

Keep commands on one control thread, with only one seek pending at a time. When you use `Transport::seek()`, the native output handles preparation and waits for acknowledgement for you. If you use the buffering methods directly, wait for that acknowledgement before preparing another position.

## Memory and failures

Both queues are allocated when you create buffered playback. Their samples take 72 KiB per track, or about 18 MiB for 256 tracks, plus decoder buffers and other small fields. You are keeping a limited amount of upcoming audio ready, rather than loading the whole song into memory.

Each time you seek, the background worker repositions the open readers and prepares fresh audio. Frequently visited positions are not cached yet.

If you request an invalid position, it is rejected before preparation starts. If decoding fails, the error is saved so your control thread can handle it. The native output silences playback if preparation fails or if the callback does not acknowledge a prepared switch within ten seconds. Those ten seconds start after preparation, so they do not limit how long reading the files can take.

After reaching the end, the worker waits for a seek request without polling. The control thread wakes it when another position is requested. Normal shutdown also wakes the worker, requests that it stop, and waits for it outside the audio callback, including when a prepared bank was never acknowledged.
