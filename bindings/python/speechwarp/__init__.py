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
import collections
import enum
import math
import unicodedata

import numpy as np

from . import _native

__version__ = _native.version()
__all__ = ["Stream", "speed_up", "SyllableCounter", "ListenerTrainer", "BlindTrials", "TrainerMeasure",
           "TrainerPlan", "TrainerParam", "WordScore", "score_words", "MIN_SPEED", "MAX_SPEED", "__version__"]

MIN_SPEED = _native.MIN_SPEED
MAX_SPEED = _native.MAX_SPEED


class Stream:
    """Speeds up speech. Write audio in, read the faster audio out.

    A stream is not thread safe: use it from one thread, or lock around it.
    """

    def __init__(self, sample_rate, channels=1, speed=1.0, nonlinear=1.0, *, pause_cap=0.0, keep_speed=True,
                 speed_floor=0.0, rhythm_gap=0.0, rhythm_rate=5.0):
        """`sample_rate` is 4000 to 384000 and `channels` 1 to 32. The rest set the properties of those names."""
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
        self.pause_cap = pause_cap
        self.keep_speed = keep_speed
        self.speed_floor = speed_floor
        self.rhythm_gap = rhythm_gap
        self.rhythm_rate = rhythm_rate

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

    # Options for very high speeds (5x to 8x). All off by default; see docs/how-it-works.md.

    @property
    def pause_cap(self):
        """Shorten every pause to at most this many seconds of input before speeding up; 0 (the default) is off.

        Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1; other values are clamped. `position` counts the frames
        left out.
        """
        return _native.get_pause_cap(self._stream)

    @pause_cap.setter
    def pause_cap(self, value):
        _native.set_pause_cap(self._stream, _number("pause_cap", value))

    @property
    def keep_speed(self):
        """While the pause cap or rhythm is on, keep the overall speed (True, the default).

        Time saved in pauses is spent playing the words slower, never below 1x, and time spent in rhythm gaps is
        made up by playing them faster. When False, trimmed pauses make playback faster than `speed`.
        """
        return _native.get_keep_speed(self._stream)

    @keep_speed.setter
    def keep_speed(self, value):
        _native.set_keep_speed(self._stream, bool(value))

    @property
    def speed_floor(self):
        """No 10 ms block of speech plays slower than this fraction of `speed`; 0 (the default) is off.

        At 8x, 0.5 keeps every block at 4x or more. Sensible: 0.3 to 0.7; clamped to 0..1, where 1 is the same as
        linear. Applies only with nonlinear speed-up above 1x.
        """
        return _native.get_speed_floor(self._stream)

    @speed_floor.setter
    def speed_floor(self, value):
        _native.set_speed_floor(self._stream, _number("speed_floor", value))

    @property
    def rhythm_gap(self):
        """Seconds of silence put into the output `rhythm_rate` times a second; 0 (the default) is off.

        Each gap goes at the quietest point nearby, faded in and out. Sensible: 0.02 to 0.06. Allowed: 0, or
        0.005 to 0.2; other values are clamped. `position` holds still during a gap.
        """
        return _native.get_rhythm_gap(self._stream)

    @rhythm_gap.setter
    def rhythm_gap(self, value):
        _native.set_rhythm_gap(self._stream, _number("rhythm_gap", value))

    @property
    def rhythm_rate(self):
        """Rhythm gaps a second of output, 1 to 16 (clamped); default 5. Sensible: 4 to 8."""
        return _native.get_rhythm_rate(self._stream)

    @rhythm_rate.setter
    def rhythm_rate(self, value):
        value = _number("rhythm_rate", value)
        if not value > 0:
            raise ValueError(f"rhythm_rate must be greater than zero, not {value}")
        _native.set_rhythm_rate(self._stream, value)

    # Options that follow the speed. Off by default; they set the pause cap and speed floor from the speed.

    def set_heard_pause(self, seconds, from_speed):
        """Keep each pause about `seconds` long in the output, from `from_speed` upward.

        The pause cap in force is `seconds` times the current speed, clamped to 0.03 to 0.4 s of input; below
        `from_speed` pauses are left alone. `seconds`: 0 turns the rule off (and the pause cap with it),
        otherwise 0.002 to 0.4. `from_speed`: 1 to 20. Sensible: 0.015 to 0.06 heard, from 3x. Setting
        `pause_cap` turns the rule off. Values are clamped.
        """
        _native.set_heard_pause(self._stream, _number("seconds", seconds), _number("from_speed", from_speed))

    @property
    def heard_pause(self):
        """The `seconds` last given to `set_heard_pause`, also while the rule is off."""
        return _native.get_heard_pause(self._stream)

    @property
    def heard_pause_from(self):
        """The `from_speed` last given to `set_heard_pause`."""
        return _native.get_heard_pause_from(self._stream)

    def set_floor_blend(self, fraction, from_speed, full_speed):
        """The speed floor in force is 0 below `from_speed`, rises linearly to `fraction` at `full_speed`, and
        stays there above it, so that a speed ramp never changes the sound in a jump.

        `fraction`: 0 turns the rule off (and the floor with it), otherwise up to 1. Speeds 1 to 20; if
        `full_speed` is not above `from_speed` the floor steps to `fraction` at `from_speed`. Sensible: 0.5
        from 4x, full at 6x. Setting `speed_floor` turns the rule off. Values are clamped.
        """
        _native.set_floor_blend(self._stream, _number("fraction", fraction), _number("from_speed", from_speed),
                                _number("full_speed", full_speed))

    @property
    def floor_blend(self):
        """The `fraction` last given to `set_floor_blend`, also while the rule is off."""
        return _native.get_floor_blend(self._stream)

    @property
    def floor_blend_from(self):
        """The `from_speed` last given to `set_floor_blend`."""
        return _native.get_floor_blend_from(self._stream)

    @property
    def floor_blend_full(self):
        """The `full_speed` last given to `set_floor_blend`."""
        return _native.get_floor_blend_full(self._stream)

    @property
    def syllable_rate(self):
        """Syllables a second in the input, pauses included, over about the last 60 s written.

        None until 10 s have been written since creation or the last reset. Multiply by the speed for the rate
        heard. An estimate, typically within about 10%.
        """
        rate = _native.syllable_rate(self._stream)
        return None if rate < 0 else rate

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


def _samples(samples, channels):
    """Check the shape of interleaved audio; returns (array, frames, is_float)."""
    samples = np.asarray(samples)
    if samples.dtype != np.int16:
        samples = samples.astype(np.float32, copy=False)
    if samples.ndim == 1 and channels == 1:
        pass
    elif samples.ndim == 2 and samples.shape[1] == channels:
        pass
    else:
        expected = "(frames,)" if channels == 1 else f"(frames, {channels})"
        raise ValueError(f"expected an array shaped {expected}, not {samples.shape}")
    return np.ascontiguousarray(samples), samples.shape[0], samples.dtype != np.int16


def _number(name, value):
    value = float(value)
    if math.isnan(value):
        raise ValueError(f"{name} must be a number")
    return value


class SyllableCounter:
    """Syllables a second in audio that does not go through a `Stream`.

    The estimator behind `Stream.syllable_rate`, on its own: given the same input it reports the same rate.
    Not thread safe.
    """

    def __init__(self, sample_rate, channels=1):
        """`sample_rate` is 4000 to 384000 and `channels` 1 to 32."""
        sample_rate = int(sample_rate)
        channels = int(channels)
        if not 4000 <= sample_rate <= 384000:
            raise ValueError(f"sample_rate must be from 4000 to 384000, not {sample_rate}")
        if not 1 <= channels <= 32:
            raise ValueError(f"channels must be from 1 to 32, not {channels}")
        self._counter = _native.syllables_create(sample_rate, channels)
        self._sample_rate = sample_rate
        self._channels = channels

    @property
    def sample_rate(self):
        return self._sample_rate

    @property
    def channels(self):
        return self._channels

    def write(self, samples):
        """Add input: an array of float32 or int16, shaped (frames,) for mono or (frames, channels)."""
        samples, frames, is_float = _samples(samples, self._channels)
        if frames:
            _native.syllables_write(self._counter, samples, frames, is_float)

    def rate(self, window_seconds=60.0, minimum_seconds=10.0):
        """Syllables a second over the last `window_seconds` written (clamped to 1 to 120), or None until
        `minimum_seconds` (0 up to the window) have been written."""
        rate = _native.syllables_rate(self._counter, _number("window_seconds", window_seconds),
                                      _number("minimum_seconds", minimum_seconds))
        return None if rate < 0 else rate

    def reset(self):
        """Forget everything written."""
        _native.syllables_reset(self._counter)


class TrainerMeasure(enum.IntEnum):
    """What a score in 0..1 measures."""

    INTELLIGIBILITY = 0  #: share of the words said back correctly from a sentence heard once
    VERIFICATION = 1  #: share right on "was this sentence in what you just heard?" items; chance is 0.5
    RETENTION = 2  #: verification items about a session's material, answered after a delay
    RATING = 3  #: the listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4


class TrainerPlan(enum.IntEnum):
    """Session plans: how the rate moves during a session."""

    STEADY = 0  #: the threshold plus a margin, all session
    RAMP = 1  #: start below the threshold and step up to threshold plus margin
    INTERVAL = 2  #: alternate periods above and below the threshold
    TRACKING = 3  #: move up or down after each in-session check, to stay at the target


class TrainerParam(enum.IntEnum):
    """Tunable numbers of the trainer, with their defaults."""

    TARGET = 0  #: share understood that defines the threshold: 0.75 (0.5 to 0.95)
    MARGIN = 1  #: steady and ramp: aim this fraction above the threshold: 0.10
    RAMP_START = 2  #: ramp: start at this fraction of the target rate: 0.8
    RAMP_STEP = 3  #: ramp: step by this fraction of the target rate: 0.02
    RAMP_MINUTES = 4  #: ramp: minutes between steps: 2
    INTERVAL_SPREAD = 5  #: interval: this fraction above, then below, the threshold: 0.15
    INTERVAL_MINUTES = 6  #: interval: minutes in each period: 10
    TRACKING_GAIN = 7  #: tracking: ln(rate) moves by gain x (score - target) per check: 0.4
    RETENTION_COST = 8  #: plans: threshold gain an hour worth a whole unit of retention: 0.2
    TEST_MAX = 9  #: threshold test: most presentations: 40
    TEST_PRECISION = 10  #: threshold test: done when the 95% interval's high / low is below this: 1.25


class ListenerTrainer:
    """Logic for training a listener to follow faster speech: no audio, no clock, no storage.

    The caller passes plain numbers in (scores, rates, its own timestamps) and gets rates and plans out. Two
    trainers created with the same seed and given the same calls give the same answers, so a caller can keep a
    log of calls and replay it to restore state. Rates are syllables a second heard (the source's syllable rate
    times the speed); times are seconds on any clock, and only differences are used. NaN arguments are
    ignored. Not thread safe.
    """

    def __init__(self, seed=0):
        """`seed` (an unsigned 64-bit integer) seeds the trainer's random choices."""
        seed = int(seed)
        if not 0 <= seed < 1 << 64:
            raise ValueError(f"seed must be an unsigned 64-bit integer, not {seed}")
        self._trainer = _native.trainer_create(seed)

    def set_weight(self, kind, weight):
        """How much a measure of each kind counts per item against the others. Negative and NaN are ignored;
        0 ignores the kind. Defaults: intelligibility 0.5, verification 1, retention 1, rating 0.3."""
        _native.trainer_set_weight(self._trainer, int(kind), float(weight))

    def get_weight(self, kind):
        return _native.trainer_get_weight(self._trainer, int(kind))

    def set_param(self, param, value):
        """Set a tunable number (see `TrainerParam` for the meanings and defaults)."""
        _native.trainer_set_param(self._trainer, int(param), float(value))

    def get_param(self, param):
        return _native.trainer_get_param(self._trainer, int(param))

    def add_measure(self, kind, score, items, rate, time):
        """A score: `kind` (not RETENTION), `score` 0..1, from `items` items, heard at `rate` syllables a
        second, at `time`. During a threshold test it updates the estimate; during a session it is an
        in-session check, and the tracking plan reacts to it. Returns False if an argument is invalid."""
        return _native.trainer_add_measure(self._trainer, int(kind), float(score), float(items), float(rate),
                                           float(time))

    # a. Threshold test

    def test_begin(self, prior_rate=0.0, time=0.0):
        """Start a threshold test (psi method). The prior is log-normal around `prior_rate`, or if that is 0,
        around the last estimate, or failing that around 10 syllables a second."""
        _native.trainer_test_begin(self._trainer, float(prior_rate), float(time))

    def test_rate(self):
        """The rate to present next, in syllables a second."""
        return _native.trainer_test_rate(self._trainer)

    @property
    def test_done(self):
        """True once the 95% interval is narrow enough (TEST_PRECISION, at least 8 presentations) or
        TEST_MAX presentations have been scored; False otherwise or if no test is running."""
        return _native.trainer_test_done(self._trainer)

    def test_end(self, time=0.0):
        """Finish the test; the estimate becomes the current threshold. Returns it (0 if no test was
        running)."""
        return _native.trainer_test_end(self._trainer, float(time))

    @property
    def threshold(self):
        """The current estimate (posterior median) of the rate understood TARGET of the time: of the running
        test, else of the last one finished; 0 if none."""
        return _native.trainer_threshold(self._trainer)

    @property
    def threshold_low(self):
        """The low end of the 95% interval of `threshold`."""
        return _native.trainer_threshold_low(self._trainer)

    @property
    def threshold_high(self):
        """The high end of the 95% interval of `threshold`."""
        return _native.trainer_threshold_high(self._trainer)

    # b. Sessions

    def session_begin(self, plan, time=0.0):
        """Begin a session under `plan` (a `TrainerPlan`)."""
        _native.trainer_session_begin(self._trainer, int(plan), float(time))

    def session_rate(self, time=0.0):
        """The rate to play at now under the session's plan; 0 if no session."""
        return _native.trainer_session_rate(self._trainer, float(time))

    def session_end(self, listening_hours, time=0.0):
        """End the session after `listening_hours` of listening. Returns the session's number (0, 1, ...) for
        `add_retention`, or -1."""
        return _native.trainer_session_end(self._trainer, float(listening_hours), float(time))

    def add_retention(self, session, score, items, delay_seconds, time=0.0):
        """Retention for a recorded session: `score` 0..1 from `items` items, answered `delay_seconds` after
        it ended. Returns False if an argument is invalid."""
        return _native.trainer_add_retention(self._trainer, int(session), float(score), float(items),
                                             float(delay_seconds), float(time))

    # c. Comparing plans

    def next_plan(self):
        """The plan to run next: a Thompson draw (advances the random source)."""
        return TrainerPlan(_native.trainer_next_plan(self._trainer))

    def plan_effect(self, plan):
        """A plan's estimated threshold gain an hour now, as a fraction (0.01 is 1% an hour)."""
        return _native.trainer_plan_effect(self._trainer, int(plan))

    def plan_effect_sd(self, plan):
        return _native.trainer_plan_effect_sd(self._trainer, int(plan))

    def plan_retention(self, plan):
        """A plan's retention; NaN without data."""
        return _native.trainer_plan_retention(self._trainer, int(plan))

    def plan_retention_sd(self, plan):
        """The standard deviation of `plan_retention`; NaN without data."""
        return _native.trainer_plan_retention_sd(self._trainer, int(plan))

    def plan_sessions(self, plan):
        """Sessions recorded for the plan."""
        return _native.trainer_plan_sessions(self._trainer, int(plan))

    def plan_best_probability(self, plan):
        """The probability that the plan is the best by utility (does not advance the random source)."""
        return _native.trainer_plan_best_probability(self._trainer, int(plan))

    @property
    def trend(self):
        """H, the hours of listening by which gains have halved (1000 stands for "not slowing")."""
        return _native.trainer_trend(self._trainer)

    @property
    def trend_sd(self):
        """The uncertainty of `trend` as a standard deviation of ln H."""
        return _native.trainer_trend_sd(self._trainer)


class BlindTrials:
    """Designs the listener's own blind A/B comparisons: which setting to compare next at a speed, which two
    of its values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6,
    ...). The caller names the settings; here they are numbers. Deterministic for a given seed."""

    def __init__(self, seed=0):
        seed = int(seed)
        if not 0 <= seed < 1 << 64:
            raise ValueError(f"seed must be an unsigned 64-bit integer, not {seed}")
        self._trials = _native.trials_create(seed)

    def add_setting(self):
        """Add a setting; returns its number (0, 1, ...), or -1."""
        return _native.trials_add_setting(self._trials)

    def add_value(self, setting, value):
        """Add a value to compare; returns its number within the setting, or -1 (duplicate, bad setting)."""
        return _native.trials_add_value(self._trials, int(setting), float(value))

    def set_available(self, setting, available):
        """Leave a setting out of `next` while False (say, when its method is not available)."""
        _native.trials_set_available(self._trials, int(setting), bool(available))

    def add(self, setting, speed, first_value, second_value, first_score, second_score, preferred):
        """Record a trial at `speed`: the two values in the order heard, each one's score 0..1, and
        `preferred`: -1 the first, 1 the second, 0 neither. Values must be ones added. Returns False if
        rejected."""
        return _native.trials_add(self._trials, int(setting), float(speed), float(first_value),
                                  float(second_value), float(first_score), float(second_score), int(preferred))

    def next(self, speed):
        """Choose the next trial at `speed`: the available setting with the fewest trials in its band, its pair
        of values compared least, in random order. Returns (setting, first_value, second_value), or None if
        no setting has two values."""
        setting, first, second = _native.trials_next(self._trials, float(speed))
        return None if setting < 0 else (setting, first, second)

    def won(self, setting, speed, value_index):
        """Comparisons the value won in the band of `speed`."""
        return _native.trials_won(self._trials, int(setting), float(speed), int(value_index))

    def lost(self, setting, speed, value_index):
        """Comparisons the value lost in the band of `speed`."""
        return _native.trials_lost(self._trials, int(setting), float(speed), int(value_index))

    def tied(self, setting, speed, value_index):
        """Comparisons the value tied in the band of `speed`."""
        return _native.trials_tied(self._trials, int(setting), float(speed), int(value_index))

    def heard(self, setting, speed, value_index):
        """Trials the value was heard in, in the band of `speed`."""
        return _native.trials_heard(self._trials, int(setting), float(speed), int(value_index))

    def mean_score(self, setting, speed, value_index):
        """The value's mean score in the band of `speed`, or None if never heard."""
        score = _native.trials_mean_score(self._trials, int(setting), float(speed), int(value_index))
        return None if math.isnan(score) else score

    def winner(self, setting, speed):
        """The index of the value with a reliable win in the band of `speed`, or None. A value wins when it has
        been heard in at least 5 trials, has met every other value in at least 3, and against each the Bayes
        factor for "preferred" over "no preference" is at least 1 / (1 - confidence)."""
        winner = _native.trials_winner(self._trials, int(setting), float(speed))
        return None if winner < 0 else winner

    def set_confidence(self, confidence):
        """Confidence for `winner`, default 0.95."""
        _native.trials_set_confidence(self._trials, float(confidence))



WordScore = collections.namedtuple("WordScore", ["share", "right", "missed", "wrong", "extra"])
WordScore.__doc__ = """How well a sentence was repeated back. `share` is right / words in the reference (0..1; with no
reference words, 1 if nothing was heard either, else 0). `right` + `missed` + `wrong` is the reference's word
count and `right` + `wrong` + `extra` the heard one's."""


def score_words(reference, heard):
    """Score `heard` (what the listener said or typed) against `reference` (the sentence played). Returns a
    WordScore.

    Both are put in Unicode form NFC, then split into words the same way: letters folded to lower case (ASCII
    and the Latin letters), punctuation dropped, an apostrophe inside a word kept (' and \u2019 alike, so
    "Don't" matches "don\u2019t"), numbers left as digits ("3" and "three" differ). The two are aligned by
    word-level edit distance; among the cheapest alignments the one with the most words right is taken. The
    rules in full are at speechwarp_score_words in include/speechwarp.h.
    """
    result = _native.score_words(unicodedata.normalize("NFC", str(reference)),
                                 unicodedata.normalize("NFC", str(heard)))
    return WordScore(*result)

def speed_up(samples, sample_rate, speed, nonlinear=1.0, **options):
    """Speed up a whole recording. Returns an array of the same type (float32 unless given int16).

    `options` are the keyword arguments of Stream: pause_cap, keep_speed, speed_floor, rhythm_gap, rhythm_rate.
    """
    samples = np.asarray(samples)
    channels = 1 if samples.ndim == 1 else samples.shape[1]
    stream = Stream(sample_rate, channels, speed=speed, nonlinear=nonlinear, **options)
    dtype = np.int16 if samples.dtype == np.int16 else np.float32
    pieces = []
    # In pieces, so that the output is collected as it is made and not held twice.
    for start in range(0, samples.shape[0], 1 << 16):
        stream.write(samples[start:start + (1 << 16)])
        pieces.append(stream.read(dtype=dtype))
    stream.flush()
    pieces.append(stream.read(dtype=dtype))
    return np.concatenate(pieces)
