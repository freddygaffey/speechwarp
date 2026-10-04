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
| Pause cap | `speechwarp_set_pause_cap`, `_get_pause_cap` | `PauseCap` | `pause_cap` | `pauseCap` | `pauseCap` | `pauseCap` |
| Keep overall speed | `speechwarp_set_keep_speed`, `_get_keep_speed` | `KeepSpeed` | `keep_speed` | `keepSpeed` | `keepSpeed` | `keepSpeed` |
| Speed floor | `speechwarp_set_speed_floor`, `_get_speed_floor` | `SpeedFloor` | `speed_floor` | `speedFloor` | `speedFloor` | `speedFloor` |
| Rhythm gap | `speechwarp_set_rhythm_gap`, `_get_rhythm_gap` | `RhythmGap` | `rhythm_gap` | `rhythmGap` | `rhythmGap` | `rhythmGap` |
| Rhythm rate | `speechwarp_set_rhythm_rate`, `_get_rhythm_rate` | `RhythmRate` | `rhythm_rate` | `rhythmRate` | `rhythmRate` | `rhythmRate` |
| Syllable rate | `speechwarp_syllable_rate` (negative: not yet) | `SyllableRate` (`double?`) | `syllable_rate` (or `None`) | `syllableRate` (`Double?`) | `syllableRate` (`Double?`) | `syllableRate` (or `null`) |
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

The same, for the other bindings:

| | Dart (Flutter) | React Native | Rust | Go | Java (desktop) |
|---|---|---|---|---|---|
| Stream type | `SpeechwarpStream` | `Stream` | `speechwarp::Stream` | `*speechwarp.Stream` | `SpeechwarpStream` |
| Create | `SpeechwarpStream(rate, channels: 1)` | `new Stream(rate, channels = 1)` | `Stream::new(rate, channels)?` | `speechwarp.NewStream(rate, channels)` | `new SpeechwarpStream(rate, channels)` |
| Destroy | `close()` | `free()` | automatic (`Drop`) | `Close()` | `close()` / try-with-resources |
| Speed | `speed` | `speed` | `speed()`, `set_speed` | `Speed()`, `SetSpeed` | `speed()`, `setSpeed` |
| Nonlinear amount | `nonlinear` | `nonlinear` | `nonlinear()`, `set_nonlinear` | `Nonlinear()`, `SetNonlinear` | `nonlinear()`, `setNonlinear` |
| Pause cap | `pauseCap` | `pauseCap` | `pause_cap()`, `set_pause_cap` | `PauseCap()`, `SetPauseCap` | `pauseCap()`, `setPauseCap` |
| Keep overall speed | `keepSpeed` | `keepSpeed` | `keep_speed()`, `set_keep_speed` | `KeepSpeed()`, `SetKeepSpeed` | `keepSpeed()`, `setKeepSpeed` |
| Speed floor | `speedFloor` | `speedFloor` | `speed_floor()`, `set_speed_floor` | `SpeedFloor()`, `SetSpeedFloor` | `speedFloor()`, `setSpeedFloor` |
| Rhythm gap | `rhythmGap` | `rhythmGap` | `rhythm_gap()`, `set_rhythm_gap` | `RhythmGap()`, `SetRhythmGap` | `rhythmGap()`, `setRhythmGap` |
| Rhythm rate | `rhythmRate` | `rhythmRate` | `rhythm_rate()`, `set_rhythm_rate` | `RhythmRate()`, `SetRhythmRate` | `rhythmRate()`, `setRhythmRate` |
| Syllable rate | `syllableRate` (`double?`) | `syllableRate` (or `null`) | `syllable_rate()` (`Option<f64>`) | `SyllableRate()` (`float64, bool`) | `syllableRate()` (`OptionalDouble`) |
| Write floats | `write(Float32List)` | `write(Float32Array)` | `write(&[f32])` | `Write([]float32)` | `write(float[], offset, length)` |
| Write 16-bit | `writeInt16(Int16List)` | - | `write_i16(&[i16])` | `WriteInt16([]int16)` | `write(short[], offset, length)` |
| Read floats | `read([maxFrames])` | `read(Float32Array)`, `read(maxFrames?)` | `read(&mut [f32])` | `Read([]float32)` | `read(float[], offset, length)` |
| Read 16-bit | `readInt16([maxFrames])` | - | `read_i16(&mut [i16])` | `ReadInt16([]int16)` | `read(short[], offset, length)` |
| Frames ready | `framesAvailable` | `available` | `available()` | `Available()` | `framesAvailable()` |
| End of input | `flush()` | `flush()` | `flush()?` | `Flush()` | `flush()` |
| Forget everything | `reset()` | `reset()` | `reset()` | `Reset()` | `reset()` |
| Position | `position` | `position` | `position()` | `Position()` | `position()` |
| Library version | `SpeechwarpStream.libraryVersion` | `version()` | `speechwarp::version()` | `speechwarp.Version()` | `SpeechwarpStream.libraryVersion()` |
| Speed limits | `minSpeed`, `maxSpeed` | `MIN_SPEED`, `MAX_SPEED` | `MIN_SPEED`, `MAX_SPEED` | `MinSpeed`, `MaxSpeed` | `MIN_SPEED`, `MAX_SPEED` |

## Behaviour common to all

**Create.** Sample rate 4000 to 384000, channels 1 to 32. Starts at speed 1 with nonlinear amount 1.

**Speed.** Clamped to 0.05..20. Applies to audio not yet processed, which includes about the last 0.15 s
written. With nonlinear speed-up it is the average speed, reached within a few seconds of input and accurate
to a few percent.

**Nonlinear amount.** 0 to 1, clamped. 1 is Speedy, 0 is even speed-up, and values between blend the two
(upstream has not tested those). Changeable at any time.

**Options for very high speeds.** Pause cap (seconds: 0 off, else 0.01 to 1), keep overall speed (on), speed
floor (fraction of the speed, 0 to 1), rhythm gap (seconds: 0 off, else 0.005 to 0.2) and rhythm rate (gaps a
second, 1 to 16, default 5). All but keep overall speed start off. Out-of-range values are clamped. Changeable
at any time; they apply to audio not yet processed. Reset keeps them. See
[How it works](how-it-works.md#options-for-very-high-speeds).

**Syllable rate.** Syllables a second in the input over the last 60 s or so, pauses included; nothing until
10 s have been written since creation or reset. An estimate, typically within about 10%.

**Write.** Interleaved samples; the length must be a whole number of frames. Floats are clipped to -1..1 and
NaN becomes 0. The output does not depend on how the input is divided between calls.

**Read.** Returns frames, and as many whole frames as fit in what you give it. May return 0 when output is
not ready yet.

**Flush.** Processes everything written, to the last frame. The stream can be written to again afterwards.

**Reset.** Discards buffered input and output and restarts the position from zero; keeps speed, nonlinear
amount and the other options, and starts the syllable count again.

**Position.** The input frame, counted from creation or the last reset, that the next output frame to be read
was made from. Never decreases; approximate to about 0.05 s of input; after a flush and reading everything,
exactly the number of frames written.

## Differences between bindings

In every binding that rejects an invalid speed, NaN for any option and a rhythm rate of zero or below are
rejected the same way; elsewhere they are ignored, as in C.

| | Invalid speed (0, negative, NaN) | Out of memory | Using a closed stream |
|---|---|---|---|
| C | ignored | returns 0 / `NULL` | undefined; do not |
| C# | `ArgumentOutOfRangeException` | `OutOfMemoryException` | `ObjectDisposedException` |
| Python | `ValueError` | `MemoryError` | cannot happen |
| Swift | ignored | throws `SpeechwarpError.outOfMemory` | cannot happen |
| Kotlin | `IllegalArgumentException` | `OutOfMemoryError` | `IllegalStateException` |
| JavaScript | `RangeError` | `Error` | `Error` |
| Dart | `ArgumentError` | `OutOfMemoryError` | `StateError` |
| React Native | `RangeError` | `Error` | `Error` |
| Rust | ignored | `Err(Error::OutOfMemory)` | cannot happen |
| Go | ignored | `ErrOutOfMemory` | `ErrClosed`, or a zero value from methods that return no error |
| Java | `IllegalArgumentException` | `OutOfMemoryError` | `IllegalStateException` |

Python returns mono as a one-dimensional array and anything else as `(frames, channels)`, and converts input
that is not float32 or int16 to float32. JavaScript works in floats only, and adds per-channel ("planar")
reads and writes because the Web Audio API works that way.
