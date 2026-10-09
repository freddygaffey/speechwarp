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

## New in 0.3.0

The stream gains two rules that follow the speed, and three new types: a syllable counter on its own, a
listener trainer and blind trials. The names follow one pattern in every binding, adapted to the language's
case: snake_case in Python and Rust, PascalCase in C# and Go, camelCase elsewhere.

| | C | C# | Python | Swift / Kotlin / Java / JS / Dart / RN | Rust | Go |
|---|---|---|---|---|---|---|
| Heard pause | `speechwarp_set_heard_pause`, `_get_heard_pause`, `_get_heard_pause_from` | `SetHeardPause(s, from)`, `HeardPause`, `HeardPauseFrom` | `set_heard_pause`, `heard_pause`, `heard_pause_from` | `setHeardPause`, `heardPause`, `heardPauseFrom` | `set_heard_pause`, `heard_pause()`, `heard_pause_from()` | `SetHeardPause`, `HeardPause()`, `HeardPauseFrom()` |
| Floor blend | `speechwarp_set_floor_blend`, `_get_floor_blend`, `_from`, `_full` | `SetFloorBlend(f, from, full)`, `FloorBlend`, `FloorBlendFrom`, `FloorBlendFull` | `set_floor_blend`, `floor_blend`, `floor_blend_from`, `floor_blend_full` | `setFloorBlend`, `floorBlend`, `floorBlendFrom`, `floorBlendFull` | `set_floor_blend`, `floor_blend()`, ... | `SetFloorBlend`, `FloorBlend()`, ... |
| Syllable counter | `speechwarp_syllables_*` | `SyllableCounter` | `SyllableCounter` | `SyllableCounter` (JS: `createSyllableCounter`) | `SyllableCounter` | `NewSyllableCounter` |
| Its rate | `_rate(window, minimum)`, negative: not yet | `double? Rate(60, 10)` | `rate()` or `None` | `rate()`, optional | `rate()` → `Option<f64>` | `Rate()` → `(float64, bool)` |
| Trainer | `speechwarp_trainer_*` | `ListenerTrainer` | `ListenerTrainer` | `ListenerTrainer` (JS: `createListenerTrainer`) | `ListenerTrainer` | `NewListenerTrainer` |
| Its enums | `SPEECHWARP_MEASURE_*`, `_PLAN_*`, `_PARAM_*` | `TrainerMeasure`, `TrainerPlan`, `TrainerParam` | the same, `IntEnum`, UPPER_CASE members | the same | the same | the same |
| Blind trials | `speechwarp_trials_*` | `BlindTrials` | `BlindTrials` | `BlindTrials` (JS: `createBlindTrials`) | `BlindTrials` | `NewBlindTrials` |
| Next trial | `_next` (-1: none), `_next_first`, `_next_second` | `(int Setting, double First, double Second)? Next(speed)` | `next(speed)` → tuple or `None` | `next(speed)` → optional record | `next()` → `Option<Trial>` | `Next()` |
| Winner | `_winner` (-1: none) | `int? Winner` | `winner()` or `None` | optional | `Option` | `(int, bool)` |

Trainer methods mirror the C functions one for one: `add_measure`, `test_begin`, `test_rate`, `test_done`,
`test_end`, `threshold` (with `_low`, `_high`), `session_begin`, `session_rate`, `session_end`,
`add_retention`, `next_plan`, `plan_effect` (with `_sd`), `plan_retention` (with `_sd`), `plan_sessions`,
`plan_best_probability`, `trend` (with `_sd`), and weights and parameters. Doubles that C returns as NaN stay
NaN. Seeds are unsigned 64-bit in C, C#, Rust, Go, Swift and Dart, the same bits in a signed `long` in Kotlin
and Java, and numbers up to 2^53 in React Native, whose module spec has no 64-bit integer.

## Word scoring

Scores a listener's repeat-back of a sentence (typed, or from speech-to-text) for the trainer: the two texts
are split into normalised words (lower case, punctuation dropped, apostrophes inside words kept, ’ read as ',
numbers left as digits) and aligned by word-level edit distance, ties going to more words right. Returns the
share right (right / reference words) and four counts. The full rules are in `include/speechwarp.h`.

| C | C# | Python | Swift | Kotlin / Java | JS | Dart | RN | Rust | Go |
|---|---|---|---|---|---|---|---|---|---|
| `speechwarp_score_words(ref, heard, counts)` → share; `counts` = right, missed, wrong, extra | `WordScore.Of(ref, heard)` → `WordScore(Share, Right, Missed, Wrong, Extra)` | `score_words(ref, heard)` → `WordScore` named tuple | `scoreWords(reference:heard:)` → `WordScore` | `WordScore.scoreWords(ref, heard)` | `speechwarp.scoreWords(ref, heard)` → `{share, right, missed, wrong, extra}` | `scoreWords(ref, heard)` → `WordScore` | `scoreWords(ref, heard)` | `score_words(ref, heard)` → `Result<WordScore, Error>` | `ScoreWords(ref, heard)` → `(WordScore, error)` |

C#, Python, Swift, Kotlin, Java, JS and React Native put both texts in Unicode form NFC first; C, Dart, Rust
and Go have no normaliser to hand, so give them text in one form (most text is NFC already).

## Speech from text (Apple only)

| | Swift (`SpeechwarpVoice`) | C# (`Speechwarp.Voice`, iOS) |
|---|---|---|
| Voices | `SpeechVoice.all`, `.eloquence(language:)`, `SpeechVoice(identifier:)` | `SpeechVoice.All`, `.Eloquence(language)`, `.Find(id)` |
| System rate | `SpeechRate.minimum`, `.normal`, `.maximum` | `SpeechRate.Minimum`, `.Normal`, `.Maximum` |
| Text to audio | `SpeechRenderer(voice:).render(_:rate:)` (async) | `new SpeechRenderer(voice).RenderAsync(text, rate)` |
| A whole text, live | `SpokenText(_:voice:voiceRate:)` | `new SpokenText(text, voice, voiceRate)` |
| Read the audio | `read(maxFrames:)` | `Read(Span<float>)` |
| Speed on top | `speed` | `Speed` |
| Other stream options | `configure { stream in ... }` | `Configure(stream => ...)` |
| Rendered ahead | `lookahead` (seconds, default 30) | `Lookahead` |
| Where it is | `characterPosition`, `sentenceIndex` | `CharacterPosition`, `SentenceIndex` |
| Jump | `seek(toCharacter:)` | `Seek(index)` |
| State | `isBuffering`, `isFinished`, `lastError` | `IsBuffering`, `IsFinished`, `LastError` |

The system delivers speech on the main thread, which must keep running; `read` never waits for rendering.
Character positions are UTF-16 offsets. Audio is mono at `sampleRate` (22,050 Hz for Eloquence).

## Speech to text with whisper.cpp (Listen)

An optional module, `listen/` (C library `speechwarp_listen`, header `listen/include/speechwarp_listen.h`), packaged
separately because it carries whisper.cpp and needs a model file. C# and Swift implement the engine-independent
interface (`ITranscriber` / `Transcriber`); Python has the same calls in its own style.

| | C# (`Speechwarp.Listen`) | Swift (`SpeechwarpListen`) | Python (`speechwarp-listen`) |
|---|---|---|---|
| Models | `WhisperModels.All`, `.Find(id)`, `.FileName(model)`, `.ForFile(path)` | `WhisperModels.all`, `.find(_:)`, `.fileName(_:)`, `.forFile(_:)` | `models()`, `find_model(id)`, `model_for_file(path)`; `Model.file_name` |
| Transcriber | `new WhisperTranscriber(path, model?, threads, useGpu)` | `WhisperTranscriber(modelPath:model:threads:useGPU:)` | `Transcriber(path, model=None, threads=0, use_gpu=False)` |
| Load | `PrepareAsync()`, `IsReady`, `IsMultilingual` | `prepare(progress:)`, `isReady`, `isMultilingual` | `prepare()`, `is_ready`, `is_multilingual` |
| A passage | `TranscribeAsync(samples, rate, options, token)` | `transcribe(_:sampleRate:options:)` | `transcribe(samples, rate, language=, word_timestamps=, hints=, preset=, threads=, cancel=)` |
| A session | `StartSession(rate, options)` → `ITranscriptionSession` | `startSession(sampleRate:options:)` → `WhisperSession` | `start_session(rate, ...)` → `Session` |
| Feeding it | `Write`; a worker thread recognises and raises `SegmentsReady` | `write`; a worker thread recognises and calls `onSegmentsReady` | `write`; `process()` returns finished segments (call it from any thread) |
| Results | `TakeSegments()`, `Partial`, `FinishAsync()` | `takeSegments()`, `partial`, `finish()`, `close()` | `take()`, `partial()`, `finish()`, `cancel()` |
| Library | `NativeVersion`, `EngineVersion`, `SystemInfo`, `SetEngineLog` | `nativeVersion`, `engineVersion`, `systemInfo`, `setEngineLog` | `native_version()`, `engine_version()`, `system_info()`, `set_engine_log()` |

Hints are joined with commas into whisper's prompt. Cancelling (a `CancellationToken`, a cancelled Swift task, a
Python `CancelToken` or `Session.cancel()`) stops the work soon and loses no audio: a cancelled finish can be called
again. Apple systems need iOS 16.4 or macOS 13.3 (whisper.cpp's use of Accelerate).

## Speech to text: the shared interface, Apple's engine, the microphone

The interface both engines implement is in the core packages (C# namespace `Speechwarp.Transcription`, Swift
module `Speechwarp`); the guide is [Speech to text](speech-to-text.md).

| | C# | Swift |
|---|---|---|
| Interface | `ITranscriber` (`Model`, `IsReady`, `PrepareAsync`, `TranscribeAsync`, `StartSession`), `ITranscriptionSession` (`Write`, `TakeSegments`, `Partial`, `SecondsWritten`, `SecondsRecognised`, `SegmentsReady`, `FinishAsync`) | `Transcriber` (`model`, `isReady`, `prepare`, `transcribe`, `startSession`), `TranscriptionSession` (`write`, `takeSegments`, `partial`, `secondsWritten`, `secondsRecognised`, `onSegmentsReady`, `finish`) |
| Types | `TranscriptionModel`, `TranscriptionOptions`, `TranscriptionEngine`, `TranscriptionPreset`, `Transcript`, `TranscriptSegment`, `TranscriptWord` | the same names |
| Apple's engine | `Speechwarp.Voice` (iOS): `new AppleTranscriber(model)`, `AppleTranscriber.Models`, `.ModelFor(language)`, `Language`; errors `AppleTranscriberException` with `AppleTranscriberError` | `SpeechwarpVoice` (iOS, macOS): `AppleTranscriber(model:recogniser:)`, `.models`, `.availableModels()`, `.model(language:)`, `recogniserInUse`, `locale`; errors `AppleTranscriberError` |
| Microphone | `MicrophoneSource(sampleRate)`: `RequestPermissionAsync()`, `StartAsync(session)`, `StartAsync(handler)`, `Stop()`, `Level`, `IsRunning`, `ConfiguresAudioSession` | `MicrophoneSource(sampleRate:)`: `requestPermission()`, `start(feeding:)`, `start(_:)`, `stop()`, `level`, `isRunning`, `configuresAudioSession`; errors `MicrophoneSourceError` |

`MicrophoneSource` feeds a session of either engine. Python has whisper.cpp only (`speechwarp-listen`, above).

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
