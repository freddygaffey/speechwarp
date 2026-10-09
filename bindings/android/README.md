# speechwarp for Android

Nonlinear speed-up for speech: listen faster and still follow it. This is the Kotlin binding of
[speechwarp](https://github.com/fredgaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```kotlin
import io.github.fredgaffey.speechwarp.SpeechwarpStream

SpeechwarpStream(sampleRate = 44100, channels = 2).use { stream ->
    stream.speed = 3f

    stream.write(input)                       // interleaved ShortArray or FloatArray
    val frames = stream.read(buffer)          // frames, not samples
    audioTrack.write(buffer, 0, frames * stream.channels)

    stream.flush()                            // at the end of the input; then read until it returns 0
}
```

It works from Java too: the optional arguments have overloads.

- `speed` is the speed you get, within a few percent. `nonlinear = 0f` switches to plain, even speed-up and
  can be changed during playback.
- `position` is the input frame being heard. The speed varies from moment to moment, so a player cannot work
  that out by multiplying. Call `reset()` after a seek.
- A stream is not thread safe. `close()` it when done.

The library is an AAR for Android 5.0 (API 21) and later, with native code for arm64-v8a, armeabi-v7a, x86_64
and x86, aligned for 16 KB pages.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```kotlin
stream.speed = 6.5f
stream.pauseCap = 0.06f   // shorten every pause to at most 60 ms
stream.speedFloor = 0.5f  // no part of the speech slower than half the speed
stream.rhythmGap = 0.04f  // a 40 ms silence...
stream.rhythmRate = 6f    // ...six times a second
// keepSpeed (true by default) holds the overall speed at 6.5x despite all of that.
```

`syllableRate` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `null` until 10 s have been written.

## Getting it

From Maven Central, once a release has been published:

```kotlin
implementation("io.github.fredgaffey:speechwarp-android:0.3.7")
```

## Building it here

Needs the Android SDK with NDK 27.1.12297006 and CMake 3.22.1, a JDK, and `cmake` on the path for the tests.

```sh
./gradlew :speechwarp:testReleaseUnitTest   # runs on this computer's JVM, against a native build for it
./gradlew :speechwarp:assembleRelease       # speechwarp/build/outputs/aar/speechwarp-release.aar
./gradlew :speechwarp:publishToMavenLocal   # to use it from another project on this computer
```

The unit tests do not run on a phone. Nothing here has been run on a device or an emulator yet.
