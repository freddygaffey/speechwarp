# Guide

speechwarp has one object, a **stream**. You write audio into it and read faster audio out of it. Everything
here applies to every language; the names are those of the C API, and the [API reference](api.md) gives each
language's spelling.

## Frames and samples

Audio is **interleaved**: for stereo, the samples go left, right, left, right. A **frame** is one sample for
each channel, so a second of 44.1 kHz stereo is 44100 frames and 88200 samples.

Counts in the API are in frames: how much is available, how much was read, the position. The exception is
the length of an array you pass, which is in samples as arrays always are. The most common mistake is to
treat the number `read` returns as samples; with stereo that plays half of what was read.

Samples are 32-bit floats from -1 to 1, or 16-bit integers. Floats outside the range are clipped. Inside, the
work is done in 16 bits, so there is nothing to gain from higher resolution.

Supported: sample rates from 4000 to 384000 Hz and 1 to 32 channels. The decision about how fast to go is
made on a mono mix of the channels, and all channels are then treated alike, so they stay in step.

## The cycle

```c
speechwarp_stream* s = speechwarp_create(44100, 2);
speechwarp_set_speed(s, 3.0f);

while (there is input) {
    speechwarp_write(s, in, frames);
    while ((n = speechwarp_read(s, out, capacity)) > 0) {
        use(out, n);
    }
}
speechwarp_flush(s);
while ((n = speechwarp_read(s, out, capacity)) > 0) {
    use(out, n);
}
speechwarp_destroy(s);
```

Three things about this are worth knowing.

**Output lags input.** The stream has to hear a little of what comes next before it can decide how fast to
play what it has. With nonlinear speed-up it holds back about 0.15 s of input; with even speed-up about
0.04 s. So `read` can return 0 straight after a `write`, and that is not an error and not the end.

**The pieces do not matter.** Writing a recording a frame at a time, in 4096-frame blocks or all at once
gives exactly the same output. Write whatever your decoder hands you.

**The stream buffers output until you read it.** Writing an hour of audio without reading makes an hour's
worth of output wait in memory. Read as you go.

## Flush and reset

`flush` means *the input has ended*. It pushes out everything the stream was holding back, including the
last fraction of a second. Call it once, at the end, and then read until empty. You may write again
afterwards, which starts a new stretch of audio.

`reset` means *forget everything*. It discards all buffered input and output and starts the position count
again from zero, keeping the speed and nonlinear settings. Call it after a seek, so that audio from the old
place is not heard at the new one.

Do not call `flush` in the middle of continuous audio to get output sooner. It works, but each flush cuts
the audio off at an arbitrary point in a sound, and the join can be heard.

## What the speed means

`set_speed(s, 3)` asks for three times as fast: ten minutes in, about three minutes twenty out. The range is
0.05 to 20, and values below 1 slow down.

With **even** speed-up (`set_nonlinear(s, 0)`) every moment is compressed alike. This is the Sonic library,
unchanged.

With **nonlinear** speed-up (the default) the speed varies from one hundredth of a second to the next: slower
through consonants and transitions, faster through steady vowels and pauses. The number you set is the
*average*. The stream measures how far its output has fallen behind and steers back, settling within a few
seconds of input.

How close is the average? Within a few percent, and more often a little fast than slow:

- The steering only ever speeds up. Time saved in a long pause is kept, not handed back by slowing the speech
  after it. A recording with long silences therefore finishes sooner than the number suggests. (With the
  pause cap or rhythm on, keep overall speed changes that: see
  [How it works](how-it-works.md#options-for-very-high-speeds).)
- Sonic itself runs up to a few percent fast at high speeds, in both modes.

If you need an exact length, measure the result and adjust, as
[`examples/blind-ab-test/app.py`](../examples/blind-ab-test/app.py) does. If you need to know where you are
during playback, do not calculate it from the speed at all: use the position.

Speed and nonlinear can be changed at any time, including during playback, with no gap or click. So can the
options for very high speeds (pause cap, keep overall speed, speed floor and rhythm). A new speed
is heard after the look-ahead, so within about 0.15 s of input.

## Position

`position` answers *which frame of the input am I hearing?* More exactly: the input frame, counted from
creation or the last `reset`, that the next frame you will read was made from.

You need it because the obvious calculation, output frames read times speed, is wrong in three ways: the
speed varies within a sentence, you may have changed it, and the average is only near the number you set.

It never goes backwards. It is approximate, within about 0.05 s of input, which is fine for a progress bar
and for remembering where someone stopped. Once you have flushed and read everything it equals exactly the
number of frames written.

It counts from the last `reset`, so a player adds the frame it seeked to:
`recording position = seek target + position`.

## Errors

Very little can go wrong. Creating a stream fails for an unsupported sample rate or channel count. `write`
and `flush` fail if memory runs out. In C these return `NULL` or 0; the bindings raise their language's usual
error. Setting the speed to zero, a negative number or NaN is ignored in C, Swift, Rust and Go and an error in
the other bindings; the [API reference](api.md#differences-between-bindings) has the table.

## Threads

A stream is not thread safe. It has no locks, so that the audio thread never waits on one it did not ask for.
Use each stream from one thread, or put your own lock around every call. Separate streams are independent.

A typical player has an audio thread that writes and reads, and a UI thread that sets the speed, seeks and
asks for the position. Those must share a lock; see [Building a player](player.md).

## Cost

On one core of an Apple M-series laptop, nonlinear speed-up processes 44.1 kHz audio about 250 times faster
than it plays, and even speed-up about 2000 times faster. Playing at 10x therefore uses about 4% of a core.
Phones and WebAssembly are slower by a small factor; measure if it matters. 48 kHz and 22.05 kHz are about
twice as cheap as 44.1 kHz, for a reason explained in [How it works](how-it-works.md).

A stream uses a few hundred kilobytes, plus whatever output is waiting to be read.
