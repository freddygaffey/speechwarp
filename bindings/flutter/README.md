# speechwarp for Flutter

Nonlinear speed-up for speech: listen faster and still follow it. This is the Flutter and Dart binding of
[speechwarp](https://github.com/fredgaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```dart
import 'package:speechwarp/speechwarp.dart';

await Speechwarp.initialize();     // once, at start-up; completes at once except on the web

final stream = SpeechwarpStream(44100, channels: 2)..speed = 3;

stream.write(samples);             // interleaved Float32List; writeInt16 for Int16List
play(stream.read());               // everything that is ready

stream.flush();                    // at the end of the input
play(stream.read());
stream.close();
```

It works on Android, iOS, macOS, Linux, Windows and the web, with the same classes on all of them. On the first
five it is an FFI plugin: native code, built with your app. On the web the library is compiled to WebAssembly
and travels inside the package, so there is nothing to host or declare as an asset.

## Starting it

Call `Speechwarp.initialize()` once, before anything else, and wait for it:

```dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Speechwarp.initialize();
  runApp(const MyApp());
}
```

On the web this compiles and starts the WebAssembly, which cannot be done without waiting. Calls made at the
same time share one load, and calling it again does nothing. Until it has completed, creating a stream (or
reading `SpeechwarpStream.libraryVersion`) on the web throws a `StateError` that says so. On every other platform
it completes at once, so the same code runs everywhere. `Speechwarp.isInitialized` says whether it has completed.

On the web:

- The calls are synchronous and run on the main thread, like everything in Dart on the web. For real-time
  playback in a browser, the [JavaScript binding](../js/) can run in an AudioWorklet, which this cannot.
- A stream, counter, trainer or trials object belongs to the page. `close()` it when done, as elsewhere; one that
  is dropped is freed some time after it is collected.
- A seed is a Dart `int`, which on the web holds whole numbers up to 2^53 exactly.
- The package works with both the JavaScript and the WebAssembly compilers of Flutter (`flutter build web` and
  `flutter build web --wasm`).

## Installing

Add it from [pub.dev](https://pub.dev/packages/speechwarp):

```yaml
dependencies:
  speechwarp: ^0.3.5
```

The package carries the C sources it compiles (and their licences), so nothing else is needed. To follow
the repository instead, depend on it through git:

```yaml
dependencies:
  speechwarp:
    git:
      url: https://github.com/fredgaffey/speechwarp
      path: bindings/flutter
```

- The speed is the speed you get, within a few percent. A nonlinear amount of 0 switches to plain, even
  speed-up and can be changed during playback.
- The position is the input frame being heard. The speed varies from moment to moment, so a player cannot
  work that out by multiplying. Reset after a seek.
- Counts are in **frames**, not samples.
- A stream is not thread safe.

The [guide](../../docs/guide.md) explains these, and [Building a player](../../docs/player.md) covers
real-time playback.
- The calls are synchronous. Converting a long recording in one go will hold up the UI; do that in an
  isolate. A stream belongs to the isolate that created it.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```dart
stream
  ..speed = 6.5
  ..pauseCap = 0.06   // shorten every pause to at most 60 ms
  ..speedFloor = 0.5  // no part of the speech slower than half the speed
  ..rhythmGap = 0.04  // a 40 ms silence...
  ..rhythmRate = 6;   // ...six times a second
// keepSpeed (true by default) holds the overall speed at 6.5x despite all of that.
```

`syllableRate` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `null` until 10 s have been written.

## Building it here

```sh
# Dart tests, on this computer's Dart VM, against a build of the library for this computer
cmake -S ../.. -B ../../build && cmake --build ../../build
SPEECHWARP_LIBRARY=$PWD/../../build/libspeechwarp.dylib flutter test     # .so on Linux

# In the real app, which proves the native library is built in and found
cd example && flutter test integration_test -d macos

# The same Dart tests in Chrome, against the library compiled to WebAssembly. This needs Emscripten (emcc on the
# path; see https://emscripten.org), and writes lib/src/wasm_bytes.dart
sh scripts/build_web.sh && flutter test --platform chrome
```

`lib/src/wasm_bytes.dart` is in git as an empty placeholder, so that the package analyses and builds on every
platform without Emscripten. `scripts/build_web.sh` overwrites it with the WebAssembly (the same compiler flags
as [`bindings/js/build.sh`](../js/build.sh)); do not commit the result, and run `git checkout lib/src/wasm_bytes.dart`
to put the placeholder back. `scripts/vendor.sh`, which prepares the package for pub.dev, runs it, so a published
package always carries the real thing. Using the package from a git checkout on the web needs `build_web.sh`
to have been run in it; without it, `Speechwarp.initialize()` throws an error that says so.

What has been run: the Dart tests and the integration test on macOS, the Dart tests in Chrome, and builds of
the example for the iOS simulator, Android and the web. Linux and Windows have not been built.
