# speechwarp for Flutter

Nonlinear speed-up for speech: listen faster and still follow it. This is the Flutter and Dart binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```dart
import 'package:speechwarp/speechwarp.dart';

final stream = SpeechwarpStream(44100, channels: 2)..speed = 3;

stream.write(samples);             // interleaved Float32List; writeInt16 for Int16List
play(stream.read());               // everything that is ready

stream.flush();                    // at the end of the input
play(stream.read());
stream.close();
```

It is an FFI plugin for Android, iOS, macOS, Linux and Windows. There is no web support; use the
[JavaScript binding](../js/) there.

## Installing

The plugin compiles the C sources at the top of this repository, so depend on it through git, not by copying
the folder:

```yaml
dependencies:
  speechwarp:
    git:
      url: https://github.com/freddygaffey/speechwarp
      path: bindings/flutter
```

It is not on pub.dev: a pub.dev package cannot reach outside its own folder, and the sources would have to
be copied into it first.

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

## Building it here

```sh
# Dart tests, on this computer's Dart VM, against a build of the library for this computer
cmake -S ../.. -B ../../build && cmake --build ../../build
SPEECHWARP_LIBRARY=$PWD/../../build/libspeechwarp.dylib flutter test     # .so on Linux

# In the real app, which proves the native library is built in and found
cd example && flutter test integration_test -d macos
```

What has been run: the Dart tests and the integration test on macOS, and builds of the example for the iOS
simulator and Android. Linux and Windows have not been built.
