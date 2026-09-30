"""Nonlinear speed-up for speech: listen faster and still follow it.

    import speechwarp
    fast = speechwarp.speed_up(samples, sample_rate, speed=3)

or, for audio that arrives in pieces:

    stream = speechwarp.Stream(sample_rate, channels=2, speed=3)
    stream.write(chunk)
    out = stream.read()
    ...
    stream.flush()
    out = stream.read()

Audio is a NumPy array of float32 in the range -1 to 1, or of int16. Mono is one-dimensional; with more
channels the shape is (frames, channels).
"""
import math

import numpy as np

from . import _native

__version__ = _native.version()
__all__ = ["Stream", "speed_up", "MIN_SPEED", "MAX_SPEED", "__version__"]

MIN_SPEED = _native.MIN_SPEED
MAX_SPEED = _native.MAX_SPEED


class Stream:
    """Speeds up speech. Write audio in, read the faster audio out.

    A stream is not thread safe: use it from one thread, or lock around it.
    """

    def __init__(self, sample_rate, channels=1, speed=1.0, nonlinear=1.0):
        """`sample_rate` is 4000 to 384000 and `channels` 1 to 32."""
        sample_rate = int(sample_rate)
        channels = int(channels)
        if not 4000 <= sample_rate <= 384000:
            raise ValueError(f"sample_rate must be from 4000 to 384000, not {sample_rate}")
        if not 1 <= channels <= 32:
            raise ValueError(f"channels must be from 1 to 32, not {channels}")
        self._stream = _native.create(sample_rate, channels)
        self._sample_rate = sample_rate
        self._channels = channels
        self.speed = speed
        self.nonlinear = nonlinear

    @property
    def sample_rate(self):
        return self._sample_rate

    @property
    def channels(self):
        return self._channels

    @property
    def speed(self):
        """Overall speed: 2 plays twice as fast. Clamped to MIN_SPEED..MAX_SPEED.

        Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
        speed-up the speed varies from moment to moment and its average is steered to this value; expect the
        result within a few percent.
        """
        return _native.get_speed(self._stream)

    @speed.setter
    def speed(self, value):
        value = float(value)
        if not value > 0 or math.isinf(value):
            raise ValueError(f"speed must be greater than zero, not {value}")
        _native.set_speed(self._stream, value)

    @property
    def nonlinear(self):
        """How unevenly time is compressed, 0 to 1.

        1 (the default) slows consonants and hurries vowels and pauses, as a fast talker does. 0 compresses
        everything evenly. May be changed at any time.
        """
        return _native.get_nonlinear(self._stream)

    @nonlinear.setter
    def nonlinear(self, value):
        value = float(value)
        if math.isnan(value):
            raise ValueError("nonlinear must be a number")
        _native.set_nonlinear(self._stream, value)

    @property
    def available(self):
        """Frames of output ready to read."""
        return _native.available(self._stream)

    @property
    def position(self):
        """The input frame, counted from creation or the last reset, that the next output frame was made from.

        This is how a player maps what is being heard back to a place in the source. It never goes backwards,
        it is approximate (within about 0.05 s of input), and once everything after a flush has been read it
        equals the number of frames written.
        """
        return _native.position(self._stream)

    def write(self, samples):
        """Add input: an array of float32 or int16, shaped (frames,) for mono or (frames, channels).

        Other types are converted to float32. The output does not depend on how the input is divided between
        calls.
        """
        samples = np.asarray(samples)
        if samples.dtype != np.int16:
            samples = samples.astype(np.float32, copy=False)
        if samples.ndim == 1 and self._channels == 1:
            frames = samples.shape[0]
        elif samples.ndim == 2 and samples.shape[1] == self._channels:
            frames = samples.shape[0]
        else:
            expected = "(frames,)" if self._channels == 1 else f"(frames, {self._channels})"
            raise ValueError(f"expected an array shaped {expected}, not {samples.shape}")
        if frames:
            _native.write(self._stream, np.ascontiguousarray(samples), frames, samples.dtype != np.int16)

    def read(self, max_frames=None, dtype=np.float32):
        """Take processed output: everything ready, or at most `max_frames` frames.

        Returns an array of `dtype` (float32 or int16), which may be empty: output lags input by a short
        look-ahead.
        """
        dtype = np.dtype(dtype)
        if dtype not in (np.dtype(np.float32), np.dtype(np.int16)):
            raise ValueError(f"dtype must be float32 or int16, not {dtype}")
        frames = self.available if max_frames is None else min(int(max_frames), self.available)
        out = np.empty((max(frames, 0), self._channels), dtype=dtype)
        if frames > 0:
            frames = _native.read(self._stream, out, frames, dtype == np.float32)
            out = out[:frames]
        return out[:, 0] if self._channels == 1 else out

    def flush(self):
        """Process everything written so far, at the end of the input. Read afterwards to get the rest.

        Writing more starts a new stretch of audio, and `position` carries on counting.
        """
        _native.flush(self._stream)

    def reset(self):
        """Discard all buffered input and output, keeping the settings. Use after seeking."""
        _native.reset(self._stream)


def speed_up(samples, sample_rate, speed, nonlinear=1.0):
    """Speed up a whole recording. Returns an array of the same type (float32 unless given int16)."""
    samples = np.asarray(samples)
    channels = 1 if samples.ndim == 1 else samples.shape[1]
    stream = Stream(sample_rate, channels, speed=speed, nonlinear=nonlinear)
    dtype = np.int16 if samples.dtype == np.int16 else np.float32
    pieces = []
    # In pieces, so that the output is collected as it is made and not held twice.
    for start in range(0, samples.shape[0], 1 << 16):
        stream.write(samples[start:start + (1 << 16)])
        pieces.append(stream.read(dtype=dtype))
    stream.flush()
    pieces.append(stream.read(dtype=dtype))
    return np.concatenate(pieces)
