# Changelog

## 0.3.5

No changes to the library. The first release that every registry publishes from GitHub with no stored
token (React Native on npm, crates.io and pub.dev switch to trusted publishing).

## 0.3.4

The first release from GitHub to every registry; 0.3.1 to 0.3.3 were tags only and were never published.

- Packages for crates.io (Rust), npm (React Native), NuGet (.NET), Maven Central (Android and desktop Java)
  and pub.dev (Flutter), each published by a version tag.
- .NET on Windows: the native library is now libspeechwarp.dll. As speechwarp.dll it had the same file name as
  the managed Speechwarp.dll, so wherever both landed in one folder every call failed.
- The project moved to github.com/fredgaffey/speechwarp. The Go module path is now
  github.com/fredgaffey/speechwarp, and the Java and Kotlin package is io.github.fredgaffey.speechwarp.
- The Android library on Maven Central is io.github.fredgaffey:speechwarp-android, separate from the desktop
  Java io.github.fredgaffey:speechwarp.

## 0.3.0 (unreleased)

Things any player can use to train for very high speeds. All off unless asked for; with them off, the output
is the same as 0.2.0's.

- **Heard pause**: the pause cap follows the speed, set as the length of pause the listener should hear (the
  cap is that times the speed, 0.03 to 0.4 s of input), from a chosen speed up. A fixed cap is heard as
  roughly the cap over the speed, so pauses all but vanished at 7.5x.
- **Floor blend**: the speed floor rises linearly from 0 at one speed to a set fraction at another, so a speed
  ramp never changes the sound in a jump. Both rules are reapplied whenever the speed changes, and each
  replaces its fixed option.
- **Syllable counter on its own** (`speechwarp_syllables_*`), the same algorithm and results as the stream's,
  with a choice of window, for players that use another speed-up.
- **Listener trainer** (`speechwarp_trainer_*`): pure logic in syllables a second heard. Threshold tests by
  the psi method with a 95% interval; four session plans (steady, ramp, interval, tracking); and Thompson
  sampling over plans on threshold gain a hour, allowing for gains that slow with practice and costing lost
  retention. Deterministic for a seed.
- **Blind trials** (`speechwarp_trials_*`): which setting and values to compare next at a speed, results by
  speed band, and a winner named only when a Bayes factor says so.
- All of it in every binding. The command-line tool gains `--heard-pause` and `--floor-blend`.
- Research notes: how comprehension is measured, the test protocol, and the trainer's design.

## 0.2.0 (unreleased)

Options for listening at 5x to 8x, all off by default; with them off, the output is the same as 0.1.0's.
See [How it works](docs/how-it-works.md#options-for-very-high-speeds).

- **Pause cap**: shorten every pause to at most a set length before speeding up.
- **Keep overall speed** (on by default while the pause cap or rhythm is on): time saved in pauses is spent
  playing the words slower, and time spent in rhythm gaps is made up, so the overall speed stays as set.
- **Speed floor**: no part of the speech slower than a set fraction of the speed.
- **Rhythm**: short silences put into the output at a regular rate, at the quietest point nearby.
- **Syllable rate**: an estimate of the syllables a second being spoken, ported from the player's C#.
- All of them in every binding. The command-line tool gains `--pause-cap`, `--floor`, `--rhythm-gap`,
  `--rhythm-rate` and `--no-keep-speed`, and `--verbose` reports syllables a second; the blind A/B example can
  compare any two settings.

## 0.1.0 (unreleased)

The first version.

- C library: streaming nonlinear (Speedy) and even (Sonic) speed-up behind `speechwarp.h`, with the average
  speed held at the one requested, and `speechwarp_position` for mapping output back to input.
- `speechwarp` command-line tool for WAV files.
- Bindings for C#/.NET, Python, Swift, Kotlin (Android), JavaScript (WebAssembly), Dart (Flutter), React
  Native, Rust, Go and desktop Java.
