# speechwarp for Go

Nonlinear speed-up for speech: listen faster and still follow it. This is the Go binding of
[speechwarp](https://github.com/fredgaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```go
import speechwarp "github.com/fredgaffey/speechwarp/bindings/go"

stream, err := speechwarp.NewStream(44100, 2) // sample rate, channels
defer stream.Close()
stream.SetSpeed(3)

stream.Write(input)             // interleaved []float32; WriteInt16 for []int16
frames := stream.Read(output)   // frames, not samples

stream.Flush()                  // at the end of the input; then Read until it returns 0
```

The C library is compiled into the package by cgo, so a C compiler is needed and nothing else. `go.mod` is at
the top of the repository, because the package compiles the C sources there; that is why the import path
ends in `bindings/go`.

- The speed is the speed you get, within a few percent. A nonlinear amount of 0 switches to plain, even
  speed-up and can be changed during playback.
- The position is the input frame being heard. The speed varies from moment to moment, so a player cannot
  work that out by multiplying. Reset after a seek.
- Counts are in **frames**, not samples.
- A stream is not thread safe.

The [guide](../../docs/guide.md) explains these, and [Building a player](../../docs/player.md) covers
real-time playback.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```go
s.SetSpeed(6.5)
s.SetPauseCap(0.06)   // shorten every pause to at most 60 ms
s.SetSpeedFloor(0.5)  // no part of the speech slower than half the speed
s.SetRhythmGap(0.04)  // a 40 ms silence...
s.SetRhythmRate(6)    // ...six times a second
// KeepSpeed (true by default) holds the overall speed at 6.5x despite all of that.
```

`SyllableRate()` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is reported as not known (`ok` is false) until 10 s have been written.

## Building it here

From the top of the repository:

```sh
go test ./bindings/go
go run ./examples/go talk.wav talk-3x.wav 3
```
