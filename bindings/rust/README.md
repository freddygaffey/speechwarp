# speechwarp for Rust

Nonlinear speed-up for speech: listen faster and still follow it. This is the Rust binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```rust
use speechwarp::Stream;

let mut stream = Stream::new(44100, 2)?;          // sample rate, channels
stream.set_speed(3.0);

stream.write(&input)?;                            // interleaved &[f32]; write_i16 for &[i16]
let frames = stream.read(&mut output);            // frames, not samples

stream.flush()?;                                  // at the end of the input; then read until it returns 0
```

The C library is compiled into the crate by its build script, so a C compiler is needed and nothing else.
`Stream` is `Send` but not `Sync`.

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

```rust
stream.set_speed(6.5);
stream.set_pause_cap(0.06);   // shorten every pause to at most 60 ms
stream.set_speed_floor(0.5);  // no part of the speech slower than half the speed
stream.set_rhythm_gap(0.04);  // a 40 ms silence...
stream.set_rhythm_rate(6.0);  // ...six times a second
// keep_speed (true by default) holds the overall speed at 6.5x despite all of that.
```

`syllable_rate()` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `None` until 10 s have been written.

## Building it here

`Cargo.toml` is at the top of the repository, because the crate compiles the C sources there.

```sh
cargo test
cargo run --release --example speed_up_wav -- talk.wav talk-3x.wav 3
```
