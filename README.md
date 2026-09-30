# speechwarp

Nonlinear speed-up for speech: listen faster and still follow it.

Ordinary speed-up compresses everything by the same amount. People who talk fast do not: they hurry through
vowels and pauses and keep consonants, which carry most of the meaning, close to their normal length.
speechwarp does the same, so speech stays easier to follow at high speeds.

It packages Google's [Speedy](https://github.com/google/speedy) algorithm (a reimplementation of MACH1:
Covell, Withgott and Slaney, ICASSP 1998) together with the [Sonic](https://github.com/waywardgeek/sonic)
library it drives, behind one small C API, with bindings for other languages to follow.

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

Nothing is published to a package
registry yet.

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

Without CMake, compile the five files in `src/` with `include/` and `third_party/kissfft/` on the include
path, and with `NDEBUG` defined: the upstream code is full of assertions.

## The command-line tool

```sh
build/speechwarp --speed 3 talk.wav talk-3x.wav            # nonlinear
build/speechwarp --speed 3 --linear talk.wav talk-3x.wav   # even, for comparison
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

[`include/speechwarp.h`](include/speechwarp.h) documents every function. Things worth knowing first:

- **The speed is the speed you get.** Speedy's own speeds average well under the one requested, because it
  slows down for consonants more than it hurries vowels. speechwarp steers the average back to the requested
  speed within a few seconds. Expect the result to land within a few percent, usually on the fast side.
- **`speechwarp_position`** says which input frame is being heard. With a speed that varies from moment to
  moment, a player cannot work that out by multiplying, so the stream keeps track.
- **`speechwarp_set_nonlinear(s, 0)`** switches to plain, even Sonic speed-up. It can be changed during
  playback without a gap.
- A stream is not thread safe. Use it from one thread or lock around it.
- Only `speechwarp_*` symbols are visible, in the static library too, so it can be linked alongside another
  copy of Sonic or KISS FFT.

On an Apple M-series laptop, one core speeds up 44.1 kHz audio about 250 times faster than it plays with
nonlinear speed-up, and about 2000 times faster without.

## How it differs from upstream

The files in `third_party/` are unmodified. What differs is in `src/`:

- `src/speechwarp.c` replaces upstream's shim between Speedy and Sonic (`soniclib.c`). It gives Speedy and
  Sonic the same data, and `tests/test_parity.c` checks the output against upstream's shim sample for sample.
  It adds the position tracking and the speed correction described above, keeps the last few milliseconds
  that upstream's flush drops, prints nothing, and reports running out of memory instead of crashing.
- `src/fft.c` stands in front of KISS FFT. Speedy analyses 30 ms windows, which at 44.1 kHz is 1322 points;
  1322 is twice a prime, and KISS FFT is slow at such sizes. The analysis ran more than 20 times slower at 44.1 kHz
  than at 48 kHz until this was added.

## Listening test

[`examples/blind-ab-test/`](examples/blind-ab-test/) is a small web app for comparing even and nonlinear
speed-up blind, on your own recordings.

## Licence

Apache-2.0. Third-party code keeps its own licence: see `NOTICE` and `third_party/`.
