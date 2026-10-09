# Speech to text

speechwarp can turn speech into text, on the device, through one interface with two engines behind it:

- **Apple's recogniser** (Swift `SpeechwarpVoice`, C# `Speechwarp.Voice`): built into iOS and macOS, nothing
  to download, no model files to manage.
- **whisper.cpp** (Swift `SpeechwarpListen`, C# `Speechwarp.Listen`, Python `speechwarp-listen`): every
  platform, a model file the app downloads, usually more accurate, with a choice of model sizes.

An app codes against the interface and picks an engine (or lets the user pick a model) at run time. The same
calls serve a sentence and a whole book:

| You have | Call | Typical use |
|---|---|---|
| A passage, all at once | `TranscribeAsync` / `transcribe` | a sentence said back for the trainer, a clip |
| Audio arriving over time, of any length | a **session**: `StartSession`, then `Write` as audio comes | an audiobook decoded in chunks, a live microphone |

The speechwarp library itself (the speed-up) does not include any of this and does not grow: speech to text is
in separate packages.

## The interface

The types are in the core packages, so code that only handles transcripts needs no engine: C# namespace
`Speechwarp.Transcription` in the `Speechwarp` package, and the `Speechwarp` Swift module.

| C# | Swift | What it is |
|---|---|---|
| `TranscriptionModel` | `TranscriptionModel` | a model: `Id` ("whisper-base.en", "apple-en-GB"), `Engine`, `Name`, `Languages`, `SizeBytes`, `DownloadUrl`, `Sha256`, `RelativeSpeed` |
| `TranscriptionOptions` | `TranscriptionOptions` | `Language`, `WordTimestamps` (on), `Hints`, `Preset` (`Fast`, `Balanced`, `Accurate`), `Threads` |
| `Transcript`, `TranscriptSegment`, `TranscriptWord` | the same | what was recognised: segments (a phrase or sentence each) with start and end in seconds, each with its words and their times and confidence |
| `ITranscriber` | `Transcriber` | `Model`, `IsReady`, `PrepareAsync`, `TranscribeAsync`, `StartSession` |
| `ITranscriptionSession` | `TranscriptionSession` | `Write`, `TakeSegments`, `Partial`, `SecondsWritten`, `SecondsRecognised`, `SegmentsReady` (`onSegmentsReady`), `FinishAsync` (`finish`) |

Rules both engines keep:

- **Audio in** is mono float samples from -1 to 1, at any rate (whisper.cpp takes 4000 to 384000 Hz). Convert
  stereo to mono first; the engines resample.
- **Times** are seconds from the start of what was given: the passage, or the session's first sample.
  Segments come in order and never start before the one before.
- **Prepare** first. `PrepareAsync` loads the whisper model, or for Apple asks the user's permission and
  installs the language if the system needs to. `TranscribeAsync` prepares by itself if needed;
  `StartSession` throws if `PrepareAsync` has not completed.
- **`Write` never waits** for recognition. The engine works on its own thread (a session's own worker for
  whisper.cpp, the system's for Apple).
- **`TakeSegments`** returns the segments finished since the last call and forgets them; `FinishAsync` ends
  the input, recognises the rest and returns everything not yet taken. Finishing again returns what is left
  (normally nothing). Writing after finishing throws in C# and is ignored in Swift.
- **`SegmentsReady`** is raised on a background thread whenever `TakeSegments` has something new. Taking from
  the handler is fine; do the slow work (saving, updating the screen) elsewhere.
- **`Partial`** is the engine's current guess at speech not yet in a finished segment, for live display; it
  changes until it becomes a finished segment, and is null when there is none and after finishing.
- **Progress** is `SecondsRecognised / SecondsWritten`.
- **Cancelling** (a `CancellationToken` in C#, cancelling the Swift task) stops a transcription soon. With
  whisper.cpp a cancelled finish loses nothing: call it again to carry on.
- **Disposing** a whisper session (or calling `close` in Swift) stops its worker at once.

Where the engines differ:

| | Apple | whisper.cpp |
|---|---|---|
| Language | fixed by the model (`apple-en-GB`); `Options.Language` is not used | `Options.Language` ("en", "de"), or null to detect it; ".en" models are English only |
| Preset | `Fast` asks the newer recogniser for quicker results; otherwise not used | `Fast` greedy without retries, `Balanced` retries when the text looks wrong, `Accurate` beam search (about twice as slow) |
| Hints | the recogniser's contextual strings | joined with commas and given to whisper as the text spoken just before |
| Word text | the recogniser's text for the word | the word with the punctuation after it ("world,") |
| Word confidence | from the system for finished words; NaN in partial guesses | the mean probability of the word's tokens |
| How a session finishes segments | as the recogniser finalises them: at pauses with SpeechAnalyzer, every 20 to 50 s with SFSpeechRecognizer | in chunks of 20 to 30 s, each cut at its quietest moment, with the text before as context |
| Partial guesses | the recogniser's own, as audio arrives | worked out from the unfinished audio (up to 30 s) with the fast preset, only while something reads `Partial`, so a book costs nothing extra |
| Platforms | Swift: iOS and macOS (SpeechAnalyzer on iOS 26 and macOS 26, SFSpeechRecognizer before). C#: iOS, SFSpeechRecognizer only, as .NET does not bind SpeechAnalyzer | Windows, Linux, macOS 13.3, Android, iOS 16.4; Python wherever its wheels are built |
| The app needs | `NSSpeechRecognitionUsageDescription` in Info.plist (and `NSMicrophoneUsageDescription` for the microphone) | the model file, downloaded by the app |

## Models

### Apple

The system manages Apple's models, so there is nothing to download yourself. `AppleTranscriber.Models` (C#) and
`AppleTranscriber.models` / `await AppleTranscriber.availableModels()` (Swift) list the languages this device can
recognise on the device; `ModelFor("en-GB")` / `model(language: "en-GB")` gives the model for one language.
`PrepareAsync` asks the user for permission the first time (the system asks only once; after a refusal the user
must allow it in Settings) and, with SpeechAnalyzer, downloads the language if the device lacks it, reporting
progress from 0 to 1.

Apple's models have size 0, no download address and a relative speed of 1.

### whisper.cpp

`WhisperModels.All` (C#), `WhisperModels.all` (Swift) and `models()` (Python) list the models an app can offer,
from the whisper.cpp project on Hugging Face, with their size and SHA-256:

| Id | Size | Languages | Relative speed |
|---|---|---|---|
| `whisper-tiny.en`, `whisper-tiny` | 78 MB | English; many | 1 |
| `whisper-base.en`, `whisper-base` | 148 MB | English; many | 2.5 |
| `whisper-small.en-q5_1` | 190 MB | English | 9 |
| `whisper-small.en`, `whisper-small` | 488 MB | English; many | 9 |
| `whisper-medium.en-q5_0`, `whisper-medium-q5_0` | 539 MB | English; many | 30 |
| `whisper-medium.en`, `whisper-medium` | 1.5 GB | English; many | 30 |
| `whisper-large-v3-turbo-q5_0` | 574 MB | many | 45 |
| `whisper-large-v3-turbo` | 1.6 GB | many | 45 |

The relative speeds are rough estimates from the models' sizes, for ordering a choice; they have not been
measured. The ".en" models are a little better at English than the multilingual ones of the same size; the
compressed (q5) ones are about a third of the size for a little accuracy. For a sentence said back, `tiny.en` or
`base.en` is a reasonable start (on an Apple M5, `tiny.en` took 0.2 s for a 3.8 s sentence). For books, a larger
model if the device can afford the time. Only `tiny.en` has been tried so far; measure the others on the
devices you care about before choosing defaults.

The library never touches the network. The app downloads the file, checks it and passes its path:

```csharp
using System.Security.Cryptography;
using Speechwarp.Listen;

var model = WhisperModels.Find("whisper-base.en")!;
var path = Path.Combine(modelsFolder, WhisperModels.FileName(model)!);   // "ggml-base.en.bin"
if (!File.Exists(path))
{
    var partial = path + ".part";
    using (var http = new HttpClient())
    await using (var file = File.Create(partial))
    await using (var body = await http.GetStreamAsync(model.DownloadUrl))
        await body.CopyToAsync(file);                                  // show model.SizeBytes as the total
    await using (var file = File.OpenRead(partial))
        if (Convert.ToHexString(await SHA256.HashDataAsync(file)).ToLowerInvariant() != model.Sha256)
            throw new InvalidDataException("The download is damaged.");
    File.Move(partial, path);
}
using var transcriber = new WhisperTranscriber(path, model);
await transcriber.PrepareAsync();                                      // loads it: up to a few seconds
```

On iOS, keep models in Application Support with the "excluded from backup" resource value set, rather than in
Documents, and use a background `URLSession` for the larger ones. `WhisperModels.ForFile(path)` describes a file the app already has, including
whisper.cpp models outside the catalogue.

Passing `useGpu: true` uses Metal on Apple systems, several times faster on a Mac with Apple silicon; it is
ignored elsewhere and in the iOS simulator.

## A passage

C#:

```csharp
var transcript = await transcriber.TranscribeAsync(samples, 16000,
    new TranscriptionOptions { Language = "en" });
Console.WriteLine(transcript.Text);
foreach (var segment in transcript.Segments)
    foreach (var word in segment.Words)
        Console.WriteLine($"{word.Start:F2}-{word.End:F2} {word.Text}");
```

Swift:

```swift
let transcript = try await transcriber.transcribe(samples, sampleRate: 16000, options: .init(language: "en"))
print(transcript.text)
```

Python:

```python
import speechwarp_listen as listen
transcriber = listen.Transcriber(path).prepare()
transcript = transcriber.transcribe(samples, 16000, language="en")
print(transcript.text)
```

The whole passage and a 16 kHz copy are held in memory, so use a session for anything over a few minutes.

## A whole book

Decode the book in chunks, write each to a session, and collect segments as they are finished. Writing is cheap
and never waits, so a decoder that runs faster than recognition must wait for it, or the backlog (held at
16 kHz, about 230 MB an hour) grows:

```csharp
using var session = transcriber.StartSession(44100,
    new TranscriptionOptions { Language = "en", Preset = TranscriptionPreset.Accurate });
session.SegmentsReady += () => store.Add(session.TakeSegments());        // on the session's thread

foreach (var chunk in decoder.MonoChunks(seconds: 10))                   // the app's decoder
{
    session.Write(chunk);
    while (session.SecondsWritten - session.SecondsRecognised > 120)    // keep two minutes ahead at most
        await Task.Delay(500);
    progress.Report(session.SecondsRecognised / bookSeconds);
}
store.Add((await session.FinishAsync()).Segments);
```

Swift is the same with `write`, `onSegmentsReady`, `takeSegments` and `finish`; Python's `Session` has no
thread of its own, so call `session.process()` between writes (or on a thread of yours) and it returns the
newly finished segments.

Times are from the start of the session. To transcribe a book in parts (across app launches, say), start a
session at each part and add the part's start time to the times it gives; begin a part at a pause so no word is
cut. Measured with whisper.cpp `tiny.en` on an Apple M5: 17.6 minutes of speech took 31 seconds, with memory
flat at about 350 MB however long the book; larger models are slower, and the GPU is faster.

## The microphone

`MicrophoneSource` captures the default input, mixes it to mono, converts it to the rate asked for and writes
it to a session of either engine:

```swift
import SpeechwarpVoice
let session = try transcriber.startSession(sampleRate: 16000, options: .init(preset: .fast))
let microphone = MicrophoneSource(sampleRate: 16000)
try await microphone.start(feeding: session)
// show session.partial as the user speaks; add session.takeSegments() to the text
microphone.stop()
let rest = try await session.finish()
```

```csharp
using Speechwarp.Voice;
using var session = transcriber.StartSession(16000, new TranscriptionOptions { Preset = TranscriptionPreset.Fast });
using var microphone = new MicrophoneSource(16000);
await microphone.StartAsync(session);
// show session.Partial; add session.TakeSegments()
microphone.Stop();
var rest = await session.FinishAsync();
```

Start the session at the microphone's `SampleRate`. `MicrophoneSource` asks for the microphone the first time
(the app needs `NSMicrophoneUsageDescription`), sets the iOS audio session to play and record unless
`ConfiguresAudioSession` is false, and reports a `Level` from 0 to 1 for a meter. It is in the Apple packages
(Swift on iOS and macOS, C# on iOS). On other platforms capture with the platform's own API and call `Write`
with mono float samples; any engine takes them.

For live display read `Partial` (a timer reading it every few hundred milliseconds is plenty) and append
finished segments as they come. With whisper.cpp, finished segments arrive every 20 to 30 seconds of speech
and the partial guess covers the rest, so the text on screen is the finished segments followed by the partial.

## Scoring a sentence said back

The trainer measures how much a listener understood at a given speed by playing a sentence and asking them to
repeat it. `speechwarp_score_words` scores the repeat-back against the sentence, in every binding:

| C# | Swift | Python | Others |
|---|---|---|---|
| `WordScore.Of(reference, heard)` | `try scoreWords(reference:heard:)` | `speechwarp.score_words(reference, heard)` | see the [API reference](api.md#word-scoring) |

Both texts are split into words the same way: lower case, punctuation dropped, an apostrophe inside a word
kept (so "Don't" matches "don’t"), hyphenated words split, numbers left as digits. The two word lists are then
aligned by word-level edit distance, and the score has the **share right** (words right / words in the
sentence) and four counts: right, missed, wrong (heard as another word) and extra.

Putting it together with the trainer:

```csharp
// The sentence was played at `rate` syllables a second; the listener's answer was recorded into `answer`.
var heard = await transcriber.TranscribeAsync(answer, 16000, new TranscriptionOptions { Language = "en" });
var score = WordScore.Of(sentence, heard.Text);
trainer.AddMeasure(TrainerMeasure.Intelligibility, score.Share,
    items: score.Right + score.Missed + score.Wrong,                  // the sentence's words
    rate: rate, time: now);
```

Things to know:

- **Do not pass the sentence as `Hints`.** It would lead the recogniser towards the right answer and inflate
  the score. Hints are for names and invented words in general.
- **Numbers.** Recognisers write numbers as digits ("3", "1990") and the scoring does not spell them out, so
  write the sentences' numbers as digits too, or leave numbers out of training sentences.
- **Short answers.** A sentence said back is a few seconds of audio, so `TranscribeAsync` with a small model
  (whisper `tiny.en` or `base.en`, or Apple's) is quick; check it on the slowest phone you support.
- **Recognition errors count against the listener.** Use the most accurate model the device runs comfortably,
  and let the listener see and correct the text before it is scored, if the app allows typing.

## Python

`speechwarp-listen` has whisper.cpp only, in Python's own style: `Transcriber(path, model=None, threads=0,
use_gpu=False)` with `prepare()`, `transcribe(samples, rate, language=, word_timestamps=, hints=, preset=,
threads=, cancel=)` and `start_session(rate, ...)`; a `Session` with `write`, `process`, `take`, `partial`,
`finish`, `cancel`, `seconds_written` and `seconds_recognised`; and `models()`, `find_model(id)` and
`model_for_file(path)`. `preset` is "fast", "balanced" or "accurate". Samples are a one-dimensional NumPy array,
or anything NumPy can turn into one of float32. A `CancelToken`
passed as `cancel=` stops a transcription from another thread.

## C

Both C# and Swift whisper packages are over a small C library, `speechwarp_listen`, in [`listen/`](../listen/)
(header [`listen/include/speechwarp_listen.h`](../listen/include/speechwarp_listen.h)), which any other language
can use. It needs the whisper.cpp submodule, which a clone does not fetch unless asked:

```sh
git submodule update --init --checkout third_party/whisper.cpp
cmake -S listen -B build-listen -G Ninja && cmake --build build-listen
build-listen/speechwarp-transcribe ggml-base.en.bin talk.wav --words
```

Only `speechwarp_listen_*` names are exported from its shared and static libraries, so it links beside another
copy of whisper.cpp or ggml without clashing.
