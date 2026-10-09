# Speechwarp.Voice

Text read aloud by Apple's voices, Eloquence among them, and sped up by speechwarp, for iOS 16 and later.
See the repository README.

## Speech to text with Apple's recogniser

`AppleTranscriber` is Apple's on-device speech recogniser behind the engine-independent `ITranscriber` interface
of the `Speechwarp` package (namespace `Speechwarp.Transcription`), so the same calls work with any engine. It
uses `SFSpeechRecognizer` with on-device recognition required: nothing leaves the device. Long input (a book, a
microphone left on) is given to the recogniser in requests of 20 to 50 s, cut in pauses; times are from the
start of the session. Apple's newer `SpeechAnalyzer` (iOS 26) is Swift-only and not bound for .NET; the Swift
package uses it.

```csharp
using Speechwarp.Transcription;
using Speechwarp.Voice;

using var transcriber = new AppleTranscriber(AppleTranscriber.ModelFor("en-GB"));
await transcriber.PrepareAsync();                        // asks the user's permission the first time

// A passage given at once: a sentence said back, a clip.
var transcript = await transcriber.TranscribeAsync(samples, 16000);

// Anything long or live: a session fed in pieces.
using var session = transcriber.StartSession(16000);
session.Write(chunk);                                    // as often as you like; never waits
var done = session.TakeSegments();                       // finished since the last call; Partial is the live guess
var rest = await session.FinishAsync();

// The microphone, into a session of any engine.
using var microphone = new MicrophoneSource(16000);
await microphone.StartAsync(session);                    // microphone.Level for a meter
microphone.Stop();
```

`AppleTranscriber.Models` lists a model per language the device recognises on the device; each has the id
`apple-<language>`, size 0 and no download URL, because the system manages them. Failures throw
`AppleTranscriberException`, whose `Error` says what went wrong (not authorised, unsupported language, ...).

The app's Info.plist needs these keys, or iOS ends the app when permission is asked for (the library checks
first and throws instead):

```xml
<key>NSSpeechRecognitionUsageDescription</key>
<string>Recognises what you say, on this device.</string>
<key>NSMicrophoneUsageDescription</key>
<string>Listens to what you say.</string>
```

The iOS simulator has no on-device recogniser ("Failed to initialize recognizer"); use a device.
