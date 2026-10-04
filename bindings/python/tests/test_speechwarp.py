import re
from pathlib import Path

import numpy as np
import pytest

import speechwarp

RATE = 22050


def signal(seconds, channels=1):
    """Something for the library to chew on: a gliding tone in bursts, with gaps."""
    t = np.arange(int(RATE * seconds)) / RATE
    phase = np.cumsum(2 * np.pi * (120 + 60 * np.sin(t * 3)) / RATE)
    envelope = np.where(t % 0.4 < 0.3, np.sin(np.pi * (t % 0.4) / 0.3), 0)
    mono = (0.4 * envelope * (np.sin(phase) + 0.5 * np.sin(2 * phase) + 0.3 * np.sin(3 * phase))).astype(np.float32)
    return mono if channels == 1 else np.repeat(mono[:, None], channels, axis=1)


def test_version_matches_the_header():
    header = Path(__file__).parents[3] / "include" / "speechwarp.h"
    if not header.exists():
        pytest.skip("not running from a checkout")
    assert speechwarp.__version__ == re.search(r'#define SPEECHWARP_VERSION "(.*)"', header.read_text()).group(1)


def test_defaults():
    stream = speechwarp.Stream(RATE)
    assert (stream.sample_rate, stream.channels, stream.speed, stream.nonlinear) == (RATE, 1, 1.0, 1.0)
    assert stream.available == 0 and stream.position == 0
    assert stream.read().shape == (0,)
    assert speechwarp.Stream(RATE, 2).read().shape == (0, 2)


def test_bad_arguments():
    with pytest.raises(ValueError):
        speechwarp.Stream(100)
    with pytest.raises(ValueError):
        speechwarp.Stream(RATE, 0)
    stream = speechwarp.Stream(RATE, 2)
    for bad in (0, -1, float("nan"), float("inf")):
        with pytest.raises(ValueError):
            stream.speed = bad
    with pytest.raises(ValueError):
        stream.nonlinear = float("nan")
    with pytest.raises(ValueError):
        stream.write(np.zeros(10, np.float32))  # mono into a stereo stream
    with pytest.raises(ValueError):
        stream.read(dtype=np.float64)


def test_clamping():
    stream = speechwarp.Stream(RATE, speed=1000, nonlinear=5)
    assert stream.speed == speechwarp.MAX_SPEED and stream.nonlinear == 1
    stream.speed, stream.nonlinear = 1e-6, -5
    assert stream.speed == pytest.approx(speechwarp.MIN_SPEED) and stream.nonlinear == 0


@pytest.mark.parametrize("speed,nonlinear", [(1, 1), (3, 1), (3, 0), (8, 1)])
def test_length(speed, nonlinear):
    samples = signal(20)
    out = speechwarp.speed_up(samples, RATE, speed, nonlinear)
    assert out.dtype == np.float32 and out.ndim == 1
    assert len(samples) / len(out) == pytest.approx(speed, rel=0.1)
    assert np.abs(out).max() <= 1


def test_speed_one_changes_nothing():
    samples = (signal(3) * 32767).astype(np.int16)
    assert np.array_equal(speechwarp.speed_up(samples, RATE, 1), samples)


def test_types_and_shapes():
    stereo = signal(5, channels=2)
    as_float = speechwarp.speed_up(stereo, RATE, 2)
    assert as_float.shape[1] == 2 and as_float.dtype == np.float32
    assert np.array_equal(as_float[:, 0], as_float[:, 1])

    as_int = speechwarp.speed_up((stereo * 32767).astype(np.int16), RATE, 2)
    assert as_int.dtype == np.int16 and as_int.shape[1] == 2
    assert len(as_int) == pytest.approx(len(as_float), rel=0.01)

    # Other types are converted, and arrays that are not contiguous are accepted.
    from_double = speechwarp.speed_up(stereo.astype(np.float64), RATE, 2)
    assert np.array_equal(from_double, as_float)
    wide = np.zeros((len(stereo), 4), np.float32)
    wide[:, ::2] = stereo
    assert np.array_equal(speechwarp.speed_up(wide[:, ::2], RATE, 2), as_float)
    assert np.array_equal(speechwarp.speed_up(stereo.tolist(), RATE, 2), as_float)


def test_streaming_matches_one_shot_and_position_follows():
    samples = signal(20)
    stream = speechwarp.Stream(RATE, speed=4)
    pieces, last = [], 0
    for start in range(0, len(samples), 1000):
        stream.write(samples[start:start + 1000])
        while stream.available >= 256:
            assert last <= stream.position <= start + 1000
            last = stream.position
            piece = stream.read(256)
            assert len(piece) == 256
            pieces.append(piece)
    stream.flush()
    pieces.append(stream.read())
    assert stream.position == len(samples)
    assert np.array_equal(np.concatenate(pieces), speechwarp.speed_up(samples, RATE, 4))


def test_reset():
    samples = signal(5)
    stream = speechwarp.Stream(RATE, speed=3)
    stream.write(samples)
    assert stream.available > 0
    stream.reset()
    assert stream.available == 0 and stream.position == 0 and stream.speed == 3
    stream.write(samples)
    stream.flush()
    assert np.array_equal(stream.read(), speechwarp.speed_up(samples, RATE, 3))


def test_high_speed_options():
    stream = speechwarp.Stream(RATE)
    assert (stream.pause_cap, stream.keep_speed, stream.speed_floor, stream.rhythm_gap) == (0, True, 0, 0)
    assert stream.rhythm_rate == 5
    assert stream.syllable_rate is None
    stream = speechwarp.Stream(RATE, pause_cap=5, keep_speed=False, speed_floor=0.5, rhythm_gap=0.04, rhythm_rate=100)
    assert stream.pause_cap == 1 and stream.keep_speed is False and stream.speed_floor == 0.5
    assert stream.rhythm_gap == pytest.approx(0.04) and stream.rhythm_rate == 16
    with pytest.raises(ValueError):
        stream.pause_cap = float("nan")
    with pytest.raises(ValueError):
        stream.rhythm_rate = 0


def test_pause_cap_shortens_and_position_reaches_the_end():
    samples = signal(10)
    stream = speechwarp.Stream(RATE, nonlinear=0, pause_cap=0.03, keep_speed=False)
    stream.write(samples)
    stream.flush()
    out = stream.read()
    assert len(out) < 0.9 * len(samples)
    assert stream.position == len(samples)
    assert stream.syllable_rate is not None


def test_speed_up_takes_options():
    samples = signal(30)  # long enough for the speed correction to settle
    kept = speechwarp.speed_up(samples, RATE, 3, rhythm_gap=0.04)
    assert abs(len(kept) / (len(samples) / 3) - 1) < 0.05
