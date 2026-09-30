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

## Building it here

From the top of the repository, which is where `pyproject.toml` is:

```sh
pip install ".[test]"
pytest
```
