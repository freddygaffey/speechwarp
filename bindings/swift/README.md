# Speechwarp for Swift

Nonlinear speed-up for speech: listen faster and still follow it. This is the Swift binding of
[speechwarp](https://github.com/fredgaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

Add the package by its repository URL, `https://github.com/fredgaffey/speechwarp`, and depend on the
`Speechwarp` product. `Package.swift` is at the top of the repository; the C sources are compiled as part of
the package, so there is nothing else to install.

```swift
import Speechwarp

let stream = try SpeechwarpStream(sampleRate: 44100, channels: 2)
stream.speed = 3

try stream.write(input)               // interleaved [Float] or [Int16], or an UnsafeBufferPointer
play(stream.read())                   // everything that is ready, as [Float]

try stream.flush()                    // at the end of the input
play(stream.read())
```

For a real-time audio callback, use `read(into:)` with a buffer of your own: it does not allocate.

- `speed` is the speed you get, within a few percent. `nonlinear = 0` switches to plain, even speed-up and can
  be changed during playback.
- `position` is the input frame being heard. The speed varies from moment to moment, so a player cannot work
  that out by multiplying. Call `reset()` after a seek.
- Counts are in **frames**, not samples.
- A stream is not thread safe.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```swift
stream.speed = 6.5
stream.pauseCap = 0.06   // shorten every pause to at most 60 ms
stream.speedFloor = 0.5  // no part of the speech slower than half the speed
stream.rhythmGap = 0.04  // a 40 ms silence...
stream.rhythmRate = 6    // ...six times a second
// keepSpeed (true by default) holds the overall speed at 6.5x despite all of that.
```

`syllableRate` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `nil` until 10 s have been written.

## Speech to text with Apple's recogniser

The `SpeechwarpVoice` product also has `AppleTranscriber`, Apple's on-device speech recogniser behind the
engine-independent `Transcriber` protocol of the `Speechwarp` product (the same calls work with any engine).
On iOS 26 and macOS 26 it uses `SpeechAnalyzer` (`SpeechTranscriber`, or `DictationTranscriber` where the device
lacks it), and `prepare` downloads the language's model if the system has not got it; on earlier systems it
uses `SFSpeechRecognizer` with on-device recognition required, given long input in requests of 20 to 50 s cut
in pauses. Nothing leaves the device.

```swift
import Speechwarp
import SpeechwarpVoice

let transcriber = try AppleTranscriber(model: AppleTranscriber.model(language: "en-GB"))
try await transcriber.prepare { fraction in print(fraction) }   // asks permission, installs the model

// A passage given at once: a sentence said back, a clip.
let transcript = try await transcriber.transcribe(samples, sampleRate: 16000, options: .init())

// Anything long or live: a session fed in pieces. Segments and words carry times from the session's start.
let session = try transcriber.startSession(sampleRate: 16000, options: .init())
session.write(chunk)                   // as often as you like; never waits
let done = session.takeSegments()      // finished since the last call; session.partial is the live guess
let rest = try await session.finish()

// The microphone, into a session of any engine.
let microphone = MicrophoneSource(sampleRate: 16000)
try await microphone.start(feeding: session)   // microphone.level for a meter
microphone.stop()
```

`AppleTranscriber.models` lists a model per language the device recognises on the device (`availableModels()`
adds the newer recogniser's languages); every one has the id `apple-<language>`, size 0 and no download URL,
because the system manages them.

An iOS app needs two keys in its Info.plist, or the system ends the app when asked for permission (the library
checks first and throws `missingUsageDescription` instead):

- `NSSpeechRecognitionUsageDescription`, for `AppleTranscriber`;
- `NSMicrophoneUsageDescription`, for `MicrophoneSource`.

A macOS app needs them too (and, if sandboxed, the Audio Input entitlement for the microphone); a command-line tool or test runner is not asked, and macOS lets it recognise on the
device without permission.

## Speech to text with whisper.cpp

The optional `SpeechwarpListen` product has `WhisperTranscriber`, whisper.cpp behind the same `Transcriber`
protocol, on iOS 16.4 and macOS 13.3 or later, with the GPU through Metal if asked (`useGPU: true`; not in the
simulator). `WhisperModels.all` lists the models an app can download, with sizes and SHA-256 hashes; the app
downloads one and passes its path.

```swift
import Speechwarp
import SpeechwarpListen

let transcriber = try WhisperTranscriber(modelPath: path)       // e.g. ggml-base.en.bin
try await transcriber.prepare(progress: nil)                     // loads the model
let transcript = try await transcriber.transcribe(samples, sampleRate: 44100, options: .init(language: "en"))
let session = try transcriber.startSession(sampleRate: 44100, options: .init())   // then as above
```

A session recognises chunks of 20 to 30 s, cut at the quietest moment, on a thread of its own; `onSegmentsReady`
is called as each is done, and `partial` is worked out only while it is read. Call `close()` on a
`WhisperSession` you abandon before `finish`.

Swift Package Manager does not build whisper.cpp (a submodule not fetched by default, built with CMake), so the
product appears only once its framework has been built into `bindings/swift/Frameworks`:

```sh
git submodule update --init --checkout third_party/whisper.cpp
bindings/swift/build-listen-xcframework.sh
SPEECHWARP_LISTEN_MODEL=/path/to/ggml-tiny.en.bin swift test --filter SpeechwarpListenTests
```

Swift Package Manager caches what Package.swift says, so after building the framework for the first time, or
deleting it, run `swift package purge-cache` once; otherwise it carries on as before (without the product, or
failing with "does not contain a binary artifact").

An app depending on this package from Git does not get the product yet. The Listen workflow builds the framework
as a zip with its checksum, ready for a binary target with a URL once it is attached to a release.

## Testing it here

```sh
swift test
```
