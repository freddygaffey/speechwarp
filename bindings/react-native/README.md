# react-native-speechwarp

Nonlinear speed-up for speech: listen faster and still follow it. This is the React Native binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```ts
import { Stream } from 'react-native-speechwarp';

const stream = new Stream(44100, 2);       // sample rate, channels
stream.speed = 3;

stream.write(samples);                     // interleaved Float32Array
play(stream.read());                       // everything that is ready

stream.flush();                            // at the end of the input
play(stream.read());
stream.free();
```

It is a C++ Turbo Module for Android and iOS, and needs the New Architecture (the default since React Native
0.76). Audio crosses to native code as the `ArrayBuffer` behind your `Float32Array`, without copying or
conversion.

- The speed is the speed you get, within a few percent. A nonlinear amount of 0 switches to plain, even
  speed-up and can be changed during playback.
- The position is the input frame being heard. The speed varies from moment to moment, so a player cannot
  work that out by multiplying. Reset after a seek.
- Counts are in **frames**, not samples.
- The calls are synchronous and run on the JavaScript thread. Call `free()` when done with a stream.

The [guide](../../docs/guide.md) explains these, and [Building a player](../../docs/player.md) covers
real-time playback.

React Native has no audio output of its own. This module turns PCM into faster PCM; getting PCM from a file
and to the speaker is for another library. If your player decodes and plays in native code, it is usually
better to use the [Android](../android/) and [Swift](../swift/) bindings there, and keep the audio off the
JavaScript thread altogether.

## Building it here

Needs Node 22, and the usual React Native toolchains for the example app.

```sh
yarn install
yarn prepare          # copies the C sources into cpp/speechwarp and generates the native interface
yarn typecheck
yarn test             # the JavaScript half, against a stand-in for the native module

cd example/ios && pod install && cd ../..
yarn example ios
yarn example android
```

An npm package holds only its own folder, so unlike the other bindings this one carries a copy of the C
sources, made by `scripts/vendor.sh`. The copy is not committed.

What has been run: the JavaScript tests; the example app on the iOS simulator, where it reported the right
result; and a build of the example for Android.
