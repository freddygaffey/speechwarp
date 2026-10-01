# speechwarp for Java

Nonlinear speed-up for speech: listen faster and still follow it. This is the desktop Java binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.
For Android, use the [Android library](../android/) instead; it has the same class.

```java
import io.github.freddygaffey.speechwarp.SpeechwarpStream;

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

## Building it here

Needs a JDK and `cmake` on the path. The native code is the Android binding's, built for this computer.

```sh
./gradlew build                  # tests, and build/libs/speechwarp-<version>.jar
./gradlew publishToMavenLocal    # to use it from another project on this computer
```
