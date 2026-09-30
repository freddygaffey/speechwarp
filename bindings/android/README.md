# speechwarp for Android

Nonlinear speed-up for speech: listen faster and still follow it. This is the Kotlin binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```kotlin
import io.github.freddygaffey.speechwarp.SpeechwarpStream

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

## Building it here

Needs the Android SDK with NDK 27.1.12297006 and CMake 3.22.1, a JDK, and `cmake` on the path for the tests.

```sh
./gradlew :speechwarp:testReleaseUnitTest   # runs on this computer's JVM, against a native build for it
./gradlew :speechwarp:assembleRelease       # speechwarp/build/outputs/aar/speechwarp-release.aar
./gradlew :speechwarp:publishToMavenLocal   # to use it from another project on this computer
```

The unit tests do not run on a phone. Nothing here has been run on a device or an emulator yet.
