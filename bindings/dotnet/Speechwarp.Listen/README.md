# Speechwarp.Listen

Speech to text on the device with [whisper.cpp](https://github.com/ggml-org/whisper.cpp), through the
engine-independent `Speechwarp.Transcription` interface of the [speechwarp](https://github.com/fredgaffey/speechwarp)
package. The same calls serve a sentence said back, a whole audiobook and a live microphone, and an app can offer
this engine and Apple's (package `Speechwarp.Voice`) side by side. It is not an official OpenAI product.

## Models

The app downloads a model file once and passes its path. `WhisperModels.All` lists those it can offer, with
names, sizes, download addresses and SHA-256 hashes; this package never uses the network itself.

```csharp
using Speechwarp.Listen;
using Speechwarp.Transcription;

foreach (var model in WhisperModels.All)
    Console.WriteLine($"{model.Name}: {model.SizeBytes / 1e6:F0} MB, {model.RelativeSpeed}x the time of the fastest");

var chosen = WhisperModels.Find("whisper-base.en")!;
string fileName = WhisperModels.FileName(chosen)!;   // "ggml-base.en.bin": download chosen.DownloadUrl to it
```

## A passage at once

```csharp
using var transcriber = new WhisperTranscriber(path);   // any whisper.cpp model file
await transcriber.PrepareAsync();                         // loads the model
var transcript = await transcriber.TranscribeAsync(samples, 44100,
    new TranscriptionOptions { Language = "en", Hints = ["Hermione", "Quidditch"] });
Console.WriteLine(transcript.Text);
foreach (var word in transcript.Segments.SelectMany(s => s.Words))
    Console.WriteLine($"{word.Start:F2}-{word.End:F2} {word.Text} ({word.Confidence:P0})");
```

Samples are mono floats, -1 to 1, at any rate from 4 to 384 kHz.

## A book, or a microphone

```csharp
using var session = transcriber.StartSession(44100);
session.SegmentsReady += () =>
{
    foreach (var segment in session.TakeSegments())
        Console.WriteLine($"[{segment.Start:F1}] {segment.Text}");
};
foreach (var chunk in DecodeInChunks(book))
    session.Write(chunk);                           // never waits for recognition
var rest = await session.FinishAsync();             // whatever was not taken yet
```

The session recognises the audio in chunks of 20 to 30 seconds, each cut at the quietest moment so that no word is
split, on a thread of its own. `SecondsRecognised / SecondsWritten` is the progress. Audio waiting to be recognised
is held in memory (about 230 MB an hour), so when decoding a book faster than it is recognised, keep
`SecondsWritten - SecondsRecognised` to a few minutes. For a microphone, read `Partial` for a quick guess at what
is being said now; it is only worked out while something reads it.

`TranscriptionOptions.Preset` trades speed for accuracy: `Fast` for a microphone, `Accurate` (beam search, about
twice as slow) for a book overnight. `new WhisperTranscriber(path, useGpu: true)` uses the GPU on Apple systems.

## Platforms

The package carries the native library for Windows (x64, arm64), Linux (x64, arm64), macOS (x64, arm64),
Android (arm64, x64, arm) and iOS 15 or later (devices, and simulators on both kinds of Mac). On x86-64 it needs
AVX2 (Intel and AMD processors from 2013 on).

## Licences

whisper.cpp and ggml are under the MIT licence; the package carries it in `licenses/whisper.cpp`. The model files
are OpenAI's Whisper models, converted by the whisper.cpp project, under the MIT licence.
