# Speechwarp for .NET

Nonlinear speed-up for speech: listen faster and still follow it. This is the .NET binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```csharp
using Speechwarp;

using var stream = new SpeechwarpStream(sampleRate: 44100, channels: 2) { Speed = 3 };

stream.Write(input);                  // interleaved floats or shorts
int frames;
while ((frames = stream.Read(buffer)) > 0)
    Play(buffer.AsSpan(0, frames * stream.Channels));

stream.Flush();                       // at the end of the input; then read until empty
```

- `Speed` is the speed you get, within a few percent. `Nonlinear = 0` switches to plain, even speed-up and
  can be changed during playback.
- `Position` is the input frame being heard. The speed varies from moment to moment, so a player cannot
  work that out by multiplying. Call `Reset()` after a seek.
- `Read` returns **frames**, not samples.
- A stream is not thread safe.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```csharp
stream.Speed = 6.5f;
stream.PauseCap = 0.06f;   // shorten every pause to at most 60 ms
stream.SpeedFloor = 0.5f;  // no part of the speech slower than half the speed
stream.RhythmGap = 0.04f;  // a 40 ms silence...
stream.RhythmRate = 6;     // ...six times a second
// KeepSpeed (true by default) holds the overall speed at 6.5x despite all of that.
```

`SyllableRate` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `null` until 10 s have been written.

## Platforms

The package carries the native library for Windows (x64, arm64), Linux (x64, arm64), macOS (x64, arm64),
Android (arm64, x64, arm) and iOS 15 or later (devices, and simulators on both kinds of Mac). On iOS it is a
framework that the package adds to the app by itself.

## Building it here

```sh
./build-native.sh                       # the native library for this machine
./build-ios.sh                          # the iOS framework, on a Mac, if the package is to include it
dotnet test --project Speechwarp.Tests
dotnet pack Speechwarp -c Release       # a package with whichever native libraries have been built
```

`build-native.sh` takes a runtime identifier to build for another platform; see the top of the script. A
package for release comes from CI (`.github/workflows/dotnet.yml`), which builds every platform.
