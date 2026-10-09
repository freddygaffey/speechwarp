# speechwarp documentation

| Read this | If you want to |
|-----------|----------------|
| [Guide](guide.md) | understand the handful of ideas every binding shares: frames, the write/read cycle, flush and reset, what the speed means |
| [Building a player](player.md) | play audio in real time: the pull loop, seeking, the progress bar, latency, threads |
| [API reference](api.md) | look up a function, with its name in each language |
| [Speech to text](speech-to-text.md) | turn speech into text on the device: a sentence said back (and scoring it for the trainer), a whole book with word times, the microphone; Apple's recogniser or whisper.cpp |
| [How it works](how-it-works.md) | know what the algorithm does to the audio, how this library differs from upstream, and what the options for 5x to 8x do |
| [Research: 5x to 8x](research-high-speed.md) | see what is known about listening at very high speeds and training for it, and the improvements proposed |
| [Examples](../examples/) | start from code that runs |
| [Contributing](../CONTRIBUTING.md) | build, test or change the library and its bindings |

Each binding also has a short README of its own with installation and a first example:
[C#](../bindings/dotnet/), [Python](../bindings/python/), [Swift](../bindings/swift/),
[Kotlin](../bindings/android/), [JavaScript](../bindings/js/), [Flutter](../bindings/flutter/),
[React Native](../bindings/react-native/), [Rust](../bindings/rust/), [Go](../bindings/go/),
[Java](../bindings/java/). For C, the header
[`include/speechwarp.h`](../include/speechwarp.h) is the reference.
