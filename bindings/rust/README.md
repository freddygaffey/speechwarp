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

## Building it here

`Cargo.toml` is at the top of the repository, because the crate compiles the C sources there.

```sh
cargo test
cargo run --release --example speed_up_wav -- talk.wav talk-3x.wav 3
```
