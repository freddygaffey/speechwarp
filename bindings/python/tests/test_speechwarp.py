import math
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


def test_heard_pause_and_floor_blend():
    stream = speechwarp.Stream(RATE)
    assert (stream.heard_pause, stream.floor_blend) == (0, 0)
    stream.set_heard_pause(0.03, 3)
    assert stream.heard_pause == pytest.approx(0.03) and stream.heard_pause_from == 3
    stream.speed = 5
    assert stream.pause_cap == pytest.approx(0.15)
    stream.speed = 2  # below from_speed
    assert stream.pause_cap == 0
    stream.speed = 5
    stream.pause_cap = 0.1  # a fixed value turns the rule off
    assert stream.pause_cap == pytest.approx(0.1)
    stream.set_floor_blend(0.5, 4, 6)
    assert (stream.floor_blend, stream.floor_blend_from, stream.floor_blend_full) == (0.5, 4, 6)
    stream.speed = 5
    assert stream.speed_floor == pytest.approx(0.25)
    stream.speed = 8
    assert stream.speed_floor == pytest.approx(0.5)
    stream.speed = 3
    assert stream.speed_floor == 0
    with pytest.raises(ValueError):
        stream.set_heard_pause(float("nan"), 3)


def test_syllable_counter_matches_the_stream():
    samples = signal(30)
    counter = speechwarp.SyllableCounter(RATE)
    stream = speechwarp.Stream(RATE)
    counter.write(samples[: 5 * RATE])
    stream.write(samples[: 5 * RATE])
    assert counter.rate() is None and stream.syllable_rate is None
    counter.write(samples[5 * RATE:])
    stream.write(samples[5 * RATE:])
    rate = counter.rate()
    assert rate is not None and rate > 0
    assert rate == pytest.approx(stream.syllable_rate)
    assert counter.rate(30, 5) is not None
    counter.write((samples * 32767).astype(np.int16))  # int16 input is accepted
    counter.reset()
    assert counter.rate() is None
    with pytest.raises(ValueError):
        counter.write(np.zeros((10, 2), np.float32))
    with pytest.raises(ValueError):
        speechwarp.SyllableCounter(100)


def test_trainer_threshold_test():
    trainer = speechwarp.ListenerTrainer(7)
    assert trainer.threshold == 0 and not trainer.test_done
    trainer.test_begin(12, 0)
    true_threshold = 14.0
    for step in range(40):
        rate = trainer.test_rate()
        assert 3 <= rate <= 60
        score = 0.95 if rate < true_threshold else 0.4
        assert trainer.add_measure(speechwarp.TrainerMeasure.INTELLIGIBILITY, score, 8, rate, step)
        if trainer.test_done:
            break
    threshold = trainer.test_end(100)
    assert threshold > 0 and threshold == trainer.threshold
    assert trainer.threshold_low < trainer.threshold < trainer.threshold_high
    assert not trainer.add_measure(speechwarp.TrainerMeasure.INTELLIGIBILITY, 2, 8, 10, 0)  # score out of range


def test_trainer_settings_and_plans():
    trainer = speechwarp.ListenerTrainer()
    assert trainer.get_param(speechwarp.TrainerParam.TARGET) == pytest.approx(0.75)
    trainer.set_param(speechwarp.TrainerParam.MARGIN, 0.2)
    assert trainer.get_param(speechwarp.TrainerParam.MARGIN) == pytest.approx(0.2)
    assert trainer.get_weight(speechwarp.TrainerMeasure.RATING) == pytest.approx(0.3)
    trainer.set_weight(speechwarp.TrainerMeasure.RATING, 0.5)
    assert trainer.get_weight(speechwarp.TrainerMeasure.RATING) == 0.5
    trainer.set_param(speechwarp.TrainerParam.MARGIN, 0.1)
    # With no data every plan has no effect and no retention.
    for plan in speechwarp.TrainerPlan:
        assert trainer.plan_effect(plan) == 0
        assert math.isnan(trainer.plan_retention(plan)) and math.isnan(trainer.plan_retention_sd(plan))
        assert trainer.plan_sessions(plan) == 0
        assert trainer.plan_effect_sd(plan) >= 0
        assert 0 <= trainer.plan_best_probability(plan) <= 1
    assert trainer.trend > 0 and trainer.trend_sd >= 0
    assert trainer.session_rate(0) == 0  # no session
    trainer.test_begin(10, 0)
    for step in range(20):
        rate = trainer.test_rate()
        trainer.add_measure(speechwarp.TrainerMeasure.VERIFICATION, 0.9 if rate < 12 else 0.55, 6, rate, step)
    threshold = trainer.test_end(50)
    trainer.session_begin(speechwarp.TrainerPlan.STEADY, 100)
    assert trainer.session_rate(100) == pytest.approx(threshold * 1.1, rel=1e-6)
    assert trainer.session_end(0.5, 3700) >= 0
    assert trainer.session_end(0.5, 3800) == -1  # no session running
    trainer.session_begin(speechwarp.TrainerPlan.STEADY, 4000)
    trainer.test_begin(0, 4100)
    trainer.add_measure(speechwarp.TrainerMeasure.INTELLIGIBILITY, 0.8, 8, threshold, 4101)
    trainer.test_end(4200)
    session = trainer.session_end(1, 4300)
    assert session >= 0
    assert trainer.add_retention(session, 0.8, 5, 86400, 90000)
    assert trainer.plan_sessions(speechwarp.TrainerPlan.STEADY) == 1


def test_trainer_is_deterministic():
    def plans(seed):
        trainer = speechwarp.ListenerTrainer(seed)
        return [trainer.next_plan() for _ in range(20)]

    assert plans(5) == plans(5)
    assert all(isinstance(plan, speechwarp.TrainerPlan) for plan in plans(5))
    assert plans(5) != plans(6)
    with pytest.raises(ValueError):
        speechwarp.ListenerTrainer(-1)


def test_blind_trials():
    trials = speechwarp.BlindTrials(3)
    assert trials.next(5) is None
    setting = trials.add_setting()
    assert setting == 0
    assert trials.add_value(setting, 0.0) == 0 and trials.add_value(setting, 0.06) == 1
    assert trials.add_value(setting, 0.06) == -1  # duplicate
    assert trials.mean_score(setting, 5, 0) is None and trials.winner(setting, 5) is None
    chosen, first, second = trials.next(5)
    assert chosen == setting and {first, second} == {0.0, 0.06}
    for _ in range(4):
        assert trials.add(setting, 5.5, 0.0, 0.06, 0.6, 0.6, 0)
    assert trials.add(setting, 5.2, 0.06, 0.0, 0.8, 0.4, -1)
    assert not trials.add(setting, 5.5, 0.0, 0.5, 0.6, 0.6, 0)  # not a value that was added
    assert (trials.won(setting, 5, 0), trials.lost(setting, 5, 0), trials.tied(setting, 5, 0)) == (0, 1, 4)
    assert (trials.won(setting, 5, 1), trials.lost(setting, 5, 1), trials.tied(setting, 5, 1)) == (1, 0, 4)
    assert trials.heard(setting, 5, 0) == 5
    assert trials.mean_score(setting, 5, 1) == pytest.approx((0.6 * 4 + 0.8) / 5)
    assert trials.winner(setting, 5) is None
    assert trials.heard(setting, 7, 0) == 0  # another band
    trials.set_available(setting, False)
    assert trials.next(5) is None
    trials.set_available(setting, True)
    trials.set_confidence(0.9)


def test_blind_trials_name_a_winner():
    trials = speechwarp.BlindTrials()
    setting = trials.add_setting()
    trials.add_value(setting, 1.0)
    trials.add_value(setting, 2.0)
    for _ in range(12):
        trials.add(setting, 6, 1.0, 2.0, 0.5, 0.9, 1)
    assert trials.winner(setting, 6) == 1


def test_score_words():
    assert speechwarp.score_words("The cat sat on the mat.", '"the CAT, sat on the mat!"') == (1.0, 6, 0, 0, 0)
    score = speechwarp.score_words("one two three four", "one too tree four five")
    assert score == speechwarp.WordScore(0.5, 2, 0, 2, 1)
    assert (score.right, score.missed, score.wrong, score.extra) == (2, 0, 2, 1)
    assert speechwarp.score_words("a b", "b a") == (0.5, 1, 1, 0, 1)
    assert speechwarp.score_words("Don't stop", "don\u2019t stop").share == 1
    assert speechwarp.score_words("Caf\u00e9 au lait", "CAF\u00c9 au lait").share == 1
    assert speechwarp.score_words("caf\u00e9", "cafe\u0301").share == 1  # normalised to NFC first
    assert speechwarp.score_words("", "") == (1.0, 0, 0, 0, 0)
    assert speechwarp.score_words("", "hello") == (0.0, 0, 0, 0, 1)
    assert speechwarp.score_words("hello world", "") == (0.0, 0, 2, 0, 0)
