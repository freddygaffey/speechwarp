# API reference

One table for every language. The C header, [`include/speechwarp.h`](../include/speechwarp.h), is the
authority; the bindings add only what their language expects, such as exceptions and properties.

| | C | C# | Python | Swift | Kotlin | JavaScript |
|---|---|---|---|---|---|---|
| Stream type | `speechwarp_stream*` | `SpeechwarpStream` | `speechwarp.Stream` | `SpeechwarpStream` | `SpeechwarpStream` | `Stream` |
| Create | `speechwarp_create(rate, channels)` | `new SpeechwarpStream(rate, channels)` | `Stream(rate, channels=1, speed=1, nonlinear=1)` | `try SpeechwarpStream(sampleRate:channels:)` | `SpeechwarpStream(rate, channels = 1)` | `(await load()).createStream(rate, channels = 1)` |
| Destroy | `speechwarp_destroy` | `Dispose()` / `using` | automatic | automatic | `close()` / `use { }` | `free()` |
| Speed | `speechwarp_set_speed`, `_get_speed` | `Speed` | `speed` | `speed` | `speed` | `speed` |
| Nonlinear amount | `speechwarp_set_nonlinear`, `_get_nonlinear` | `Nonlinear` | `nonlinear` | `nonlinear` | `nonlinear` | `nonlinear` |
| Write floats | `speechwarp_write` | `Write(ReadOnlySpan<float>)` | `write(array)` | `write([Float])`, `write(UnsafeBufferPointer<Float>)` | `write(FloatArray, offset, length)` | `write(Float32Array)`, `writePlanar([...])` |
| Write 16-bit | `speechwarp_write_i16` | `Write(ReadOnlySpan<short>)` | `write(int16 array)` | `write([Int16])` | `write(ShortArray, offset, length)` | - |
| Read floats | `speechwarp_read` | `Read(Span<float>)` | `read(max_frames=None)` | `read(into:)`, `read(maxFrames:)` | `read(FloatArray, offset, length)` | `read(Float32Array)`, `read(maxFrames?)`, `readPlanar([...])` |
| Read 16-bit | `speechwarp_read_i16` | `Read(Span<short>)` | `read(dtype=np.int16)` | `read(into:)` | `read(ShortArray, offset, length)` | - |
| Frames ready | `speechwarp_available` | `FramesAvailable` | `available` | `framesAvailable` | `framesAvailable` | `available` |
| End of input | `speechwarp_flush` | `Flush()` | `flush()` | `flush()` | `flush()` | `flush()` |
| Forget everything | `speechwarp_reset` | `Reset()` | `reset()` | `reset()` | `reset()` | `reset()` |
| Position | `speechwarp_position` | `Position` | `position` | `position` | `position` | `position` |
| Library version | `speechwarp_version()` | `SpeechwarpStream.NativeVersion` | `speechwarp.__version__` | `SpeechwarpStream.libraryVersion` | `SpeechwarpStream.libraryVersion` | `(await load()).version` |
| Speed limits | `SPEECHWARP_MIN_SPEED`, `_MAX_SPEED` | `MinSpeed`, `MaxSpeed` | `MIN_SPEED`, `MAX_SPEED` | `speedRange` | `MIN_SPEED`, `MAX_SPEED` | `MIN_SPEED`, `MAX_SPEED` |
| Whole recording at once | - | - | `speechwarp.speed_up(samples, rate, speed, nonlinear=1)` | - | - | - |

## Behaviour common to all

**Create.** Sample rate 4000 to 384000, channels 1 to 32. Starts at speed 1 with nonlinear amount 1.

**Speed.** Clamped to 0.05..20. Applies to audio not yet processed, which includes about the last 0.15 s
written. With nonlinear speed-up it is the average speed, reached within a few seconds of input and accurate
to a few percent.

**Nonlinear amount.** 0 to 1, clamped. 1 is Speedy, 0 is even speed-up, and values between blend the two
(upstream has not tested those). Changeable at any time.

**Write.** Interleaved samples; the length must be a whole number of frames. Floats are clipped to -1..1 and
NaN becomes 0. The output does not depend on how the input is divided between calls.

**Read.** Returns frames, and as many whole frames as fit in what you give it. May return 0 when output is
not ready yet.

**Flush.** Processes everything written, to the last frame. The stream can be written to again afterwards.

**Reset.** Discards buffered input and output and restarts the position from zero; keeps speed and nonlinear
amount.

**Position.** The input frame, counted from creation or the last reset, that the next output frame to be read
was made from. Never decreases; approximate to about 0.05 s of input; after a flush and reading everything,
exactly the number of frames written.

## Differences between bindings

| | Invalid speed (0, negative, NaN) | Out of memory | Using a closed stream |
|---|---|---|---|
| C | ignored | returns 0 / `NULL` | undefined; do not |
| C# | `ArgumentOutOfRangeException` | `OutOfMemoryException` | `ObjectDisposedException` |
| Python | `ValueError` | `MemoryError` | cannot happen |
| Swift | ignored | throws `SpeechwarpError.outOfMemory` | cannot happen |
| Kotlin | `IllegalArgumentException` | `OutOfMemoryError` | `IllegalStateException` |
| JavaScript | `RangeError` | `Error` | `Error` |

Python returns mono as a one-dimensional array and anything else as `(frames, channels)`, and converts input
that is not float32 or int16 to float32. JavaScript works in floats only, and adds per-channel ("planar")
reads and writes because the Web Audio API works that way.
