# speechwarp

[![PyPI](https://img.shields.io/pypi/v/speechwarp?label=PyPI)](https://pypi.org/project/speechwarp/)
[![npm](https://img.shields.io/npm/v/speechwarp?label=npm)](https://www.npmjs.com/package/speechwarp)
[![NuGet](https://img.shields.io/nuget/v/Speechwarp?label=NuGet)](https://www.nuget.org/packages/Speechwarp)
[![crates.io](https://img.shields.io/crates/v/speechwarp?label=crates.io)](https://crates.io/crates/speechwarp)
[![pub.dev](https://img.shields.io/pub/v/speechwarp?label=pub.dev)](https://pub.dev/packages/speechwarp)
[![Maven Central](https://img.shields.io/maven-central/v/io.github.fredgaffey/speechwarp?label=Maven%20Central)](https://central.sonatype.com/namespace/io.github.fredgaffey)
[![React Native](https://img.shields.io/npm/v/react-native-speechwarp?label=react-native)](https://www.npmjs.com/package/react-native-speechwarp)
[![Go](https://pkg.go.dev/badge/github.com/fredgaffey/speechwarp/bindings/go.svg)](https://pkg.go.dev/github.com/fredgaffey/speechwarp/bindings/go)

Nonlinear speed-up for speech: listen faster and still follow it.

Ordinary speed-up compresses everything by the same amount. People who talk fast do not: they hurry through
vowels and pauses and keep consonants, which carry most of the meaning, close to their normal length.
speechwarp does the same, so speech stays easier to follow at high speeds.

It packages Google's [Speedy](https://github.com/google/speedy) algorithm (a reimplementation of MACH1:
Covell, Withgott and Slaney, ICASSP 1998) together with the [Sonic](https://github.com/waywardgeek/sonic)
library it drives, behind one small C API, with bindings for many languages.

**This is not an official Google product.** It redistributes their Apache-2.0 code; see [NOTICE](NOTICE).

## Status

Early. The C library, its tests and the command-line tool work on macOS; Linux and Windows are built by CI
but have had no other use yet. Bindings:

| Language | Where | Platforms |
|----------|-------|-----------|
| C# / .NET | [`bindings/dotnet/`](bindings/dotnet/) | Windows, Linux, macOS, Android, iOS |
| Python | [`bindings/python/`](bindings/python/) | anywhere with a C compiler; NumPy arrays in and out |
| Swift | [`bindings/swift/`](bindings/swift/) | macOS, iOS, tvOS, watchOS |
| Kotlin / Java | [`bindings/android/`](bindings/android/) | Android 5.0 and later |
| JavaScript / TypeScript | [`bindings/js/`](bindings/js/) | browsers, Node, AudioWorklet (WebAssembly) |
| Dart / Flutter | [`bindings/flutter/`](bindings/flutter/) | Android, iOS, macOS, Linux, Windows, web |
| React Native | [`bindings/react-native/`](bindings/react-native/) | Android, iOS (New Architecture) |
| Rust | [`bindings/rust/`](bindings/rust/) | anywhere with a C compiler |
| Go | [`bindings/go/`](bindings/go/) | anywhere with a C compiler (cgo) |
| Java (desktop) | [`bindings/java/`](bindings/java/) | Linux, macOS, Windows |

## Installing

Every package is published from this repository's GitHub Actions on each release, built and tested on the
platforms it supports.

| Language | Registry | Install |
|----------|----------|---------|
| Python | [PyPI](https://pypi.org/project/speechwarp/) | `pip install speechwarp` |
| JavaScript / TypeScript | [npm](https://www.npmjs.com/package/speechwarp) | `npm install speechwarp` |
| C# / .NET | [NuGet](https://www.nuget.org/packages/Speechwarp) | `dotnet add package Speechwarp` |
| Rust | [crates.io](https://crates.io/crates/speechwarp) | `cargo add speechwarp` |
| Dart / Flutter | [pub.dev](https://pub.dev/packages/speechwarp) | `flutter pub add speechwarp` |
| React Native | [npm](https://www.npmjs.com/package/react-native-speechwarp) | `npm install react-native-speechwarp` |
| Go | [pkg.go.dev](https://pkg.go.dev/github.com/fredgaffey/speechwarp/bindings/go) | `go get github.com/fredgaffey/speechwarp/bindings/go` |
| Kotlin (Android) | [Maven Central](https://central.sonatype.com/artifact/io.github.fredgaffey/speechwarp-android) | `implementation("io.github.fredgaffey:speechwarp-android:0.3.7")` |
| Java (desktop) | [Maven Central](https://central.sonatype.com/artifact/io.github.fredgaffey/speechwarp) | `implementation("io.github.fredgaffey:speechwarp:0.3.7")` |
| Swift | Swift Package Manager | `.package(url: "https://github.com/fredgaffey/speechwarp", from: "0.3.7")`, product `Speechwarp` |
| C | source | build with CMake, below |

The Maven lines are for Gradle (`build.gradle.kts`); with Maven, use group `io.github.fredgaffey`, artifact
`speechwarp-android` or `speechwarp`. For Swift, add the package in Xcode under File, Add Package
Dependencies, or in `Package.swift` as shown. Each binding's README has a first example.

## Speech from text, on Apple devices

An optional module reads text aloud with Apple's voices, Eloquence among them (the compact voice many fast
screen-reader listeners use, included from iOS 16 and macOS 13), and speeds it up with speechwarp, so the same
speeds and high-speed options work as for recordings. It renders sentence by sentence a little ahead of playback,
in the background, and reports which character is being spoken. Swift: the `SpeechwarpVoice` product of the
Swift package. C#: the `Speechwarp.Voice` package (`dotnet add package Speechwarp.Voice`, iOS 16 and later).

```swift
import SpeechwarpVoice
let reed = SpeechVoice.eloquence(language: "en-US").first { $0.name == "Reed" }!
let book = try SpokenText(text, voice: reed, voiceRate: SpeechRate.maximum)   // about 4x by itself
book.speed = 1.5                                                              // speechwarp on top
let samples = book.read(maxFrames: 1024)          // on the audio thread; mono at book.sampleRate
let at = book.characterPosition                   // for highlighting
```

## Speech to text

Optional packages turn speech into text on the device, through one interface with two engines: Apple's
recogniser (in `SpeechwarpVoice` and `Speechwarp.Voice`, nothing to download) and whisper.cpp (`SpeechwarpListen`
for Swift, `Speechwarp.Listen` for .NET on Windows, Linux, macOS, Android and iOS, `speechwarp-listen` for
Python; the app downloads a model, listed with its size and hash). The same calls serve a sentence said back for
the listener trainer, a whole audiobook with word times, and a live microphone. `Speechwarp.Listen` and
`speechwarp-listen` are published from the next release; until then build them from this repository. See
[Speech to text](docs/speech-to-text.md).

```csharp
using var transcriber = new WhisperTranscriber(modelPath);          // or new AppleTranscriber(...)
var transcript = await transcriber.TranscribeAsync(samples, 16000);
var score = WordScore.Of(sentence, transcript.Text);                 // for the trainer: share of words right
```

## Documentation

Start with the [guide](docs/guide.md): frames, the write and read cycle, what the speed means. Then:

- [Building a player](docs/player.md) - playing in real time, seeking, the progress bar, threads
- [API reference](docs/api.md) - every function, in every language
- [Speech to text](docs/speech-to-text.md) - transcribing a sentence, a book or the microphone, and scoring
  a sentence said back
- [How it works](docs/how-it-works.md) - what the algorithm does, and what this library adds to upstream
- [Examples](examples/) - code that runs, in each language
- [Contributing](CONTRIBUTING.md) - building and testing everything

## Building

Needs a C compiler and CMake 3.16 or newer. There are no other dependencies.

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

This builds the static and shared libraries, the `speechwarp` tool and the tests. `cmake --install build`
installs the libraries, `speechwarp.h` and a CMake package, which another project uses with
`find_package(speechwarp)` and the target `speechwarp::shared` or `speechwarp::static`.

Without CMake, compile the six `.c` files in `src/` with `include/` and `third_party/kissfft/` on the include
path, and with `NDEBUG` defined: the upstream code is full of assertions.

## The command-line tool

```sh
build/speechwarp --speed 3 talk.wav talk-3x.wav            # nonlinear
build/speechwarp --speed 3 --linear talk.wav talk-3x.wav   # even, for comparison
build/speechwarp --speed 6.5 --pause-cap 0.06 --floor 0.5 -v talk.wav talk-6x.wav
```

It reads PCM or 32-bit float WAV and writes 16-bit PCM. `--help` lists the options.

## The library

```c
#include "speechwarp.h"

speechwarp_stream* s = speechwarp_create(44100, 2);   /* sample rate, channels */
speechwarp_set_speed(s, 3.0f);

while (more_input) {
    speechwarp_write(s, in, frames);                  /* interleaved floats */
    while ((n = speechwarp_read(s, out, capacity)) > 0) {
        play(out, n);
    }
}
speechwarp_flush(s);                                  /* then read until empty */
speechwarp_destroy(s);
```

[`include/speechwarp.h`](include/speechwarp.h) documents every function, and
[`examples/c/player.c`](examples/c/player.c) is the loop of a real-time player. Things worth knowing first:

- **The speed is the speed you get.** Speedy's own speeds average well under the one requested, because it
  slows down for consonants more than it hurries vowels. speechwarp steers the average back to the requested
  speed within a few seconds. Expect the result to land within a few percent, usually on the fast side.
- **`speechwarp_position`** says which input frame is being heard. With a speed that varies from moment to
  moment, a player cannot work that out by multiplying, so the stream keeps track.
- **`speechwarp_set_nonlinear(s, 0)`** switches to plain, even Sonic speed-up. It can be changed during
  playback without a gap.
- **For 5x to 8x** there are options to cap pauses, set a floor under Speedy's speeds and put a regular
  rhythm of short gaps into the output, while keeping the overall speed. All are off by default; see
  [How it works](docs/how-it-works.md#options-for-very-high-speeds). `speechwarp_syllable_rate` estimates how
  many syllables a second are being spoken.
- A stream is not thread safe. Use it from one thread or lock around it.
- Only `speechwarp_*` symbols are visible, in the static library too, so it can be linked alongside another
  copy of Sonic or KISS FFT.

On an Apple M-series laptop, one core speeds up 44.1 kHz audio about 250 times faster than it plays with
nonlinear speed-up, and about 2000 times faster without.

## Licence

Apache-2.0. Third-party code keeps its own licence: see `NOTICE` and `third_party/`.
