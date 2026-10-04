# speechwarp for Python

Nonlinear speed-up for speech: listen faster and still follow it. This is the Python binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library. It is not an official Google product.

```python
import soundfile
import speechwarp

samples, sample_rate = soundfile.read("talk.wav", dtype="float32")
fast = speechwarp.speed_up(samples, sample_rate, speed=3)
soundfile.write("talk-3x.wav", fast, sample_rate)
```

For audio that arrives in pieces:

```python
stream = speechwarp.Stream(sample_rate, channels=2, speed=3)
for chunk in chunks:
    stream.write(chunk)
    play(stream.read())
stream.flush()
play(stream.read())
```

Audio is a NumPy array of float32 in the range -1 to 1, or of int16. Mono is one-dimensional; with more
channels the shape is `(frames, channels)`.

- `speed` is the speed you get, within a few percent. `nonlinear=0` gives plain, even speed-up, and both can
  be changed on a stream at any time.
- `stream.position` is the input frame being heard. The speed varies from moment to moment, so a player
  cannot work that out by multiplying. Call `stream.reset()` after a seek.
- A stream is not thread safe.

## Very high speeds

For 5x and beyond, a few options take some of the speed from pauses rather than words and give the
listener a rhythm to follow. All are off by default. What each does, and how they did in a first test, is in
[How it works](../../docs/how-it-works.md#options-for-very-high-speeds).

```python
stream = speechwarp.Stream(44100, speed=6.5,
                           pause_cap=0.06,    # shorten every pause to at most 60 ms
                           speed_floor=0.5,   # no part of the speech slower than half the speed
                           rhythm_gap=0.04,   # a 40 ms silence...
                           rhythm_rate=6)     # ...six times a second
# keep_speed (True by default) holds the overall speed at 6.5x despite all of that.
# speechwarp.speed_up() takes the same keyword arguments.
```

`stream.syllable_rate` is an estimate of the syllables a second in the input, over the last minute; multiply it by the
speed for the rate heard. It is `None` until 10 s have been written.

## Building it here

From the top of the repository, which is where `pyproject.toml` is:

```sh
pip install ".[test]"
pytest
```
