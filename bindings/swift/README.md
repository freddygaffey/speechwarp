# Speechwarp for Swift

Nonlinear speed-up for speech: listen faster and still follow it. This is the Swift binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

Add the package by its repository URL, `https://github.com/freddygaffey/speechwarp`, and depend on the
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

## Testing it here

```sh
swift test
```
