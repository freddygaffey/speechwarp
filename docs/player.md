# Building a player

A converter pushes: read a file, write it to the stream, save what comes out. A player pulls: the sound card
asks for a block of audio every few milliseconds and must get it on time. This page is about the second
kind. Complete versions of the loop are in [`examples/c/player.c`](../examples/c/player.c) (runs with no
audio device), [`examples/android/SpeechPlayer.kt`](../examples/android/SpeechPlayer.kt) and
[`bindings/js/example/speechwarp-processor.js`](../bindings/js/example/speechwarp-processor.js).

## The pull loop

When the device wants `N` frames, feed the stream until it has `N` ready, then read them.

```c
int render(player* p, float* out, int frames) {
    while (speechwarp_available(p->stream) < frames && !p->ended) {
        int n = decode_next(p, p->input, FEED_BLOCK);      /* your decoder */
        if (n == 0) {
            speechwarp_flush(p->stream);                   /* end of the recording */
            p->ended = 1;
        } else {
            speechwarp_write(p->stream, p->input, n);
        }
    }
    int got = speechwarp_read(p->stream, out, frames);
    /* fewer than asked for only at the very end: fill the rest with silence */
    return got;
}
```

At speed *s* one block of output consumes about *s* blocks of input, so the loop runs several times per
callback at high speeds. That is expected. Size the feed block so the loop is not starved: about a thousand
frames works well.

Do the decoding on the audio thread only if your decoder is quick and never blocks on a file or the network.
Otherwise decode on another thread into a queue, and have the audio thread take from the queue.

## Starting and the look-ahead

The first read after creating or resetting a stream needs about 0.15 s of input written before anything comes
out (0.04 s with even speed-up). In a pull loop this looks after itself: the first callback simply feeds more
before it can read. It costs a fraction of a millisecond of computing, not 0.15 s of waiting, because the
input is already there to be fed.

For a live source, where input arrives only as fast as it is spoken, that look-ahead is real delay.

## The progress bar

```c
long position_in_recording = p->base + speechwarp_position(p->stream);
```

`base` is the frame you last seeked to, zero at the start. Do not compute the position from elapsed time and
the speed; see [Position](guide.md#position).

Audio the stream has handed over still has to get through the sound card's own buffer before it is heard, so
the position runs ahead of the ear by the length of that buffer times the speed. With a 50 ms buffer at 3x
that is 0.15 s of the recording. Subtract it if you care; most players do not.

## Seeking

```c
decoder_seek(p, frame);
speechwarp_reset(p->stream);
p->base = frame;
p->ended = 0;
```

`reset` discards what was buffered for the old place and keeps the speed and nonlinear settings. Without it,
up to 0.15 s of the old place plays at the new one.

## Changing speed and mode

Just set them. There is no gap, click or need to reset. A new speed is heard after the look-ahead. Switching
between nonlinear and even speed-up plays the held-back audio at the even speed and carries on, losing
nothing.

If you offer speeds up to 10x, keep even speed-up selectable. Which is easier to follow depends on the
listener and the narrator; [`examples/blind-ab-test`](../examples/blind-ab-test/) lets someone find out for
themselves.

## Pausing

Stop calling the stream. It holds its state for as long as you like, and carries on exactly where it was.
Do not flush on pause.

## Threads

The audio thread writes and reads. The UI thread sets the speed, seeks and reads the position. A stream has
no locks of its own, so put every call behind one lock:

```kotlin
val frames = synchronized(lock) { feed(); stream.read(output) }   // audio thread
fun seekTo(frame: Long) = synchronized(lock) { ... stream.reset() ... }   // UI thread
```

Hold the lock only around calls into the stream, never while waiting on the sound card. The calls are short:
at 10x, producing a 10 ms block takes well under a millisecond.

If you would rather have no lock on the audio thread, send the UI's requests to it through a queue and apply
them at the top of the callback, and publish the position back through an atomic variable.

## End of the recording

When the decoder runs out, `flush` once and keep reading until `read` returns less than you asked for. After
that, `position` equals the length of what was written, so the progress bar lands exactly on the end.

To play the next chapter without a gap, do not flush between them: just keep writing. Flush only at the true
end.
