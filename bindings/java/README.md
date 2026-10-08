# speechwarp for Java

Nonlinear speed-up for speech: listen faster and still follow it. This is the desktop Java binding of
[speechwarp](https://github.com/fredgaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.
For Android, use the [Android library](../android/) instead; it has the same class.

```java
import io.github.fredgaffey.speechwarp.SpeechwarpStream;

try (SpeechwarpStream stream = new SpeechwarpStream(44100, 2)) {   // sample rate, channels
    stream.setSpeed(3);

    stream.write(input);                       // interleaved short[] or float[]
    int frames = stream.read(buffer);          // frames, not samples
    line.write(...);

    stream.flush();                            // at the end of the input; then read until it returns 0
}
```

The JAR holds the native library and unpacks it when the class loads. A JAR built on one computer holds only
that computer's; CI builds one with Linux, macOS and Windows inside. Java 11 or later.

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

```java
stream.setSpeed(6.5f);
stream.setPauseCap(0.06f);   // shorten every pause to at most 60 ms
stream.setSpeedFloor(0.5f);  // no part of the speech slower than half the speed
stream.setRhythmGap(0.04f);  // a 40 ms silence...
stream.setRhythmRate(6);     // ...six times a second
// keepSpeed() (true by default) holds the overall speed at 6.5x despite all of that.
```

`syllableRate()` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is empty until 10 s have been written.

## Getting it

From Maven Central, once a release has been published (the JAR carries native libraries for macOS, Linux and
Windows):

```kotlin
implementation("io.github.fredgaffey:speechwarp:0.3.0")
```

## Building it here

Needs a JDK and `cmake` on the path. The native code is the Android binding's, built for this computer.

```sh
./gradlew build                  # tests, and build/libs/speechwarp-<version>.jar
./gradlew publishToMavenLocal    # to use it from another project on this computer
```
