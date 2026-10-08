/* speechwarp - nonlinear speed-up for speech.
 *
 * A small, stable C API over Google's Speedy algorithm and the Sonic library.
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * Usage:
 *     speechwarp_stream* s = speechwarp_create(44100, 2);
 *     speechwarp_set_speed(s, 3.0f);
 *     for each chunk of input:
 *         speechwarp_write(s, in, frames);
 *         while ((n = speechwarp_read(s, out, capacity)) > 0) play(out, n);
 *     speechwarp_flush(s);  then read until empty
 *     speechwarp_destroy(s);
 *
 * Samples are interleaved 32-bit floats in [-1, 1], or 16-bit integers. A "frame" is one sample per channel.
 * A stream is not thread safe: use it from one thread, or lock around it.
 */
#ifndef SPEECHWARP_H_
#define SPEECHWARP_H_

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SPEECHWARP_BUILD)
#    define SPEECHWARP_API __declspec(dllexport)
#  elif defined(SPEECHWARP_SHARED)
#    define SPEECHWARP_API __declspec(dllimport)
#  else
#    define SPEECHWARP_API
#  endif
#else
#  define SPEECHWARP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SPEECHWARP_VERSION_MAJOR 0
#define SPEECHWARP_VERSION_MINOR 3
#define SPEECHWARP_VERSION_PATCH 0
#define SPEECHWARP_VERSION "0.3.0"

#define SPEECHWARP_MIN_SPEED 0.05f
#define SPEECHWARP_MAX_SPEED 20.0f

typedef struct speechwarp_stream speechwarp_stream;

/* The version of the library in use, e.g. "0.3.0". */
SPEECHWARP_API const char* speechwarp_version(void);

/* Create a stream. Returns NULL if the arguments are invalid or memory runs out. The sample rate must be
 * 4000 to 384000 Hz and there may be 1 to 32 channels. Starts at speed 1 with nonlinear speed-up on. */
SPEECHWARP_API speechwarp_stream* speechwarp_create(int sample_rate, int channels);
SPEECHWARP_API void speechwarp_destroy(speechwarp_stream* stream);

/* Overall speed: 2 plays twice as fast, so ten minutes of speech comes out in about five. Clamped to
 * SPEECHWARP_MIN_SPEED..SPEECHWARP_MAX_SPEED; zero, negative and NaN are ignored. Takes effect on audio not
 * yet processed, which includes the last 0.15 s or so written.
 *
 * With nonlinear speed-up the speed varies from moment to moment, and the stream steers its average to this
 * value within a few seconds of input. It only steers by speeding up, so a recording with long silences
 * finishes somewhat sooner than the speed suggests (but see speechwarp_set_keep_speed). */
SPEECHWARP_API void speechwarp_set_speed(speechwarp_stream* stream, float speed);
SPEECHWARP_API float speechwarp_get_speed(const speechwarp_stream* stream);

/* How unevenly time is compressed, 0 to 1. 1 (the default) is Speedy: consonants and transitions are slowed
 * and vowels and pauses hurried, as a fast talker does. 0 compresses everything evenly (plain Sonic). Values
 * in between are allowed but untested upstream. May be changed at any time. */
SPEECHWARP_API void speechwarp_set_nonlinear(speechwarp_stream* stream, float amount);
SPEECHWARP_API float speechwarp_get_nonlinear(const speechwarp_stream* stream);

/* Add input. Returns 1 on success, 0 if memory ran out. The output does not depend on how the input is
 * divided between calls. */
SPEECHWARP_API int speechwarp_write(speechwarp_stream* stream, const float* samples, int frames);
SPEECHWARP_API int speechwarp_write_i16(speechwarp_stream* stream, const int16_t* samples, int frames);

/* Take processed output. Returns the number of frames written to `samples`, which may be 0: output lags
 * input by a look-ahead of about 0.15 s of input with nonlinear speed-up, and 0.04 s without. */
SPEECHWARP_API int speechwarp_read(speechwarp_stream* stream, float* samples, int max_frames);
SPEECHWARP_API int speechwarp_read_i16(speechwarp_stream* stream, int16_t* samples, int max_frames);

/* Frames of output ready to read. */
SPEECHWARP_API int speechwarp_available(const speechwarp_stream* stream);

/* Process everything written so far, at the end of the input. Returns 1 on success, 0 if memory ran out.
 * Writing more afterwards starts a new stretch of audio; position carries on counting. */
SPEECHWARP_API int speechwarp_flush(speechwarp_stream* stream);

/* Discard all buffered input and output, keeping speed and nonlinear settings. Use after seeking. In the
 * unlikely event that memory runs out here, the stream stops accepting input: write and flush return 0. */
SPEECHWARP_API void speechwarp_reset(speechwarp_stream* stream);

/* The input frame, counted from creation or the last reset, that the next output frame to be read was made
 * from. Because speed varies from moment to moment, this is how a player maps what is being heard back to
 * a position in the source. It never goes backwards, it is approximate (it can run up to about 0.05 s of
 * input ahead), and once everything after a flush has been read it equals the number of frames written. */
SPEECHWARP_API int64_t speechwarp_position(const speechwarp_stream* stream);

/* ---- Options for very high speeds (5x to 8x) ----------------------------------------------------------
 *
 * At 6x, audiobook narration would come out at 24 to 30 syllables a second, beyond the 17 to 22 that the best
 * trained listeners follow. These options take some of the speed from pauses instead of words, stop Speedy
 * from squeezing the relaxed parts unbearably hard, and give the listener a rhythm to lock on to. All are off
 * by default, and with them off the output is the same as without them. Like the speed, each may be changed
 * at any time and takes effect on audio not yet processed; NaN is ignored and other values are clamped. See
 * docs/how-it-works.md and docs/research-high-speed.md. */

/* Pause cap: shorten every pause to at most `seconds` of input before speeding up, so that the speed is spent
 * on words. 0 turns it off (the default). Sensible values are 0.04 to 0.2; 0.06 keeps a pause just long enough
 * to hear that there was one. Allowed: 0, or 0.01 to 1. A pause is any stretch 30 dB or more below the level
 * of recent speech; the part left out is crossfaded so that it does not click. Works with nonlinear speed-up on
 * and off, and speechwarp_position counts the frames left out. */
SPEECHWARP_API void speechwarp_set_pause_cap(speechwarp_stream* stream, float seconds);
SPEECHWARP_API float speechwarp_get_pause_cap(const speechwarp_stream* stream);

/* Keep overall speed: 1 (the default) or 0. Takes effect only while the pause cap or rhythm is on. When on,
 * time saved by the pause cap is spent playing the words slower, and time spent in rhythm gaps is made up by
 * playing them faster, so that 6x still finishes in a sixth of the time. The speech is never slowed below 1x,
 * and the correction never more than doubles the speed. When off, trimmed pauses make playback faster than
 * the set speed, and gaps make it slower. */
SPEECHWARP_API void speechwarp_set_keep_speed(speechwarp_stream* stream, int enabled);
SPEECHWARP_API int speechwarp_get_keep_speed(const speechwarp_stream* stream);

/* Speed floor, as a fraction of the speed: no 10 ms block of speech plays slower than this times the speed.
 * 0 turns it off (the default). Speedy may slow the tensest speech all the way to 1x, so at 8x the rest has to
 * run far faster than 8x; a floor of 0.5 keeps every block at 4x or more, and evens out the effort. Allowed: 0
 * to 1, where 1 is the same as linear speed-up. Sensible values are 0.3 to 0.7. Applies only to speeds above 1
 * with nonlinear speed-up on. */
SPEECHWARP_API void speechwarp_set_speed_floor(speechwarp_stream* stream, float fraction);
SPEECHWARP_API float speechwarp_get_speed_floor(const speechwarp_stream* stream);

/* Rhythm (Ghitza and Greenberg, 2009): put a short silence into the output at a regular rate, which at 3x
 * restored much of what compression alone lost. `seconds` is the length of each gap; 0 turns it off (the
 * default). Sensible values are 0.02 to 0.06. Allowed: 0, or 0.005 to 0.2. Each gap goes at the quietest point
 * near where the rate puts it, with 5 ms fades either side. speechwarp_position holds still during a gap. Gaps
 * never take more than half the output. Rhythm adds about 0.1 s of output latency. */
SPEECHWARP_API void speechwarp_set_rhythm_gap(speechwarp_stream* stream, float seconds);
SPEECHWARP_API float speechwarp_get_rhythm_gap(const speechwarp_stream* stream);

/* Rhythm rate: gaps a second of output, 1 to 16, default 5. Sensible values are 4 to 8, the rate of
 * syllables in ordinary speech. Zero, negative and NaN are ignored. */
SPEECHWARP_API void speechwarp_set_rhythm_rate(speechwarp_stream* stream, float per_second);
SPEECHWARP_API float speechwarp_get_rhythm_rate(const speechwarp_stream* stream);

/* Syllables a second in the input, pauses included, over the last 60 s or so written (since creation or the
 * last reset), or a negative number until 10 s have been written. Multiply by the speed for the rate heard.
 * An estimate from the loudness of voiced sound (de Jong and Wempe, 2009): on synthetic speech it came
 * within -12% to +1% of the true count. Always running; it costs little. */
SPEECHWARP_API double speechwarp_syllable_rate(const speechwarp_stream* stream);

/* ---- Options that follow the speed ----------------------------------------------------------------------
 *
 * The pause cap and the speed floor above are fixed numbers, but what they do depends on the speed. These two
 * rules work out, for each speed, what to give speechwarp_set_pause_cap and speechwarp_set_speed_floor, and
 * apply it whenever the speed changes (by speechwarp_set_speed, at any time, including the small steps of a
 * training ramp). Both are off by default. Setting the matching fixed option turns a rule off, and turning a
 * rule on replaces the fixed value; speechwarp_get_pause_cap and speechwarp_get_speed_floor always return the
 * value in force. Reset keeps them. NaN is ignored and other values are clamped.
 *
 * Why the pause is given in seconds heard: the pause cap is measured in input, so a fixed cap is heard as
 * roughly the cap divided by the speed: 0.06 s became 36 ms at 2x and 8 ms at 7.5x when measured
 * (docs/how-it-works.md). A listener needs a pause of roughly the same length
 * in their own time at any speed to hear the break. */

/* Heard pause: keep each pause about `seconds` long in the output. The getters return the values last set,
 * also while the rule is off; a NaN in any argument ignores the call. The pause cap in force is `seconds` times
 * the current speed, clamped to 0.03 to 0.4 s of input. Below `from_speed` pauses are left alone (pause cap 0).
 * `seconds`: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to 0.4. `from_speed`: 1 to 20.
 * Sensible values are 0.015 to 0.06 heard, from 3x. */
SPEECHWARP_API void speechwarp_set_heard_pause(speechwarp_stream* stream, float seconds, float from_speed);
SPEECHWARP_API float speechwarp_get_heard_pause(const speechwarp_stream* stream);
SPEECHWARP_API float speechwarp_get_heard_pause_from(const speechwarp_stream* stream);

/* Floor blend: the speed floor in force is 0 below `from_speed`, rises linearly to `fraction` at `full_speed`,
 * and stays at `fraction` above it, so that a speed ramp never changes the sound in a jump. `fraction`: 0 turns
 * the rule off (and the floor with it); otherwise up to 1. Speeds 1 to 20; if `full_speed` is not above
 * `from_speed` it is taken as equal, and the floor steps to `fraction` at `from_speed`. Sensible: 0.5 from 4x,
 * full at 6x. */
SPEECHWARP_API void speechwarp_set_floor_blend(speechwarp_stream* stream, float fraction, float from_speed,
                                               float full_speed);
SPEECHWARP_API float speechwarp_get_floor_blend(const speechwarp_stream* stream);
SPEECHWARP_API float speechwarp_get_floor_blend_from(const speechwarp_stream* stream);
SPEECHWARP_API float speechwarp_get_floor_blend_full(const speechwarp_stream* stream);

/* ---- Syllable counter -----------------------------------------------------------------------------------
 *
 * The estimator behind speechwarp_syllable_rate, on its own, for audio that does not go through a stream (a
 * player using some other speed-up, or measuring a file). Same algorithm and the same results: a counter
 * given the same input as a stream reports the same rate as the stream does with window 60 and minimum 10.
 * Float input is converted to 16-bit exactly as the stream converts it. Not thread safe. */

typedef struct speechwarp_syllables speechwarp_syllables;

/* Sample rate 4000 to 384000, channels 1 to 32; NULL if invalid or out of memory. */
SPEECHWARP_API speechwarp_syllables* speechwarp_syllables_create(int sample_rate, int channels);
SPEECHWARP_API void speechwarp_syllables_destroy(speechwarp_syllables* counter);
/* Interleaved samples, as speechwarp_write. Returns 1, or 0 for invalid arguments. */
SPEECHWARP_API int speechwarp_syllables_write(speechwarp_syllables* counter, const float* samples, int frames);
SPEECHWARP_API int speechwarp_syllables_write_i16(speechwarp_syllables* counter, const int16_t* samples,
                                                  int frames);
/* Syllables a second over the last `window_seconds` written (or all of it, if less), or a negative number
 * until `minimum_seconds` have been written. Window clamped to 1 to 120 s, minimum to 0 to the window. */
SPEECHWARP_API double speechwarp_syllables_rate(const speechwarp_syllables* counter, double window_seconds,
                                                double minimum_seconds);
/* Forget everything written. */
SPEECHWARP_API void speechwarp_syllables_reset(speechwarp_syllables* counter);

/* ---- Listener trainer -----------------------------------------------------------------------------------
 *
 * Pure logic for training a listener to follow faster speech: no audio, no clock, no storage. The caller
 * passes plain numbers in (scores, rates, its own timestamps) and gets rates and plans out. Deterministic: two
 * trainers created with the same seed and given the same calls give the same answers, so a caller keeps its
 * own log of calls and replays it into a new trainer to restore state.
 *
 * The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times
 * are seconds on any clock the caller likes (Unix time, say), and only differences are used. docs/research-
 * high-speed.md explains the design and the research behind each part. NaN arguments are ignored. */

typedef struct speechwarp_trainer speechwarp_trainer;

/* What a score in 0..1 measures. */
enum {
  /* Share of the words said back correctly from a sentence heard once. */
  SPEECHWARP_MEASURE_INTELLIGIBILITY = 0,
  /* Share right on "was this sentence in what you just heard?" items (the sentence verification technique,
   * Royer et al.: originals and paraphrases against changed-meaning and unrelated sentences). Chance is 0.5. */
  SPEECHWARP_MEASURE_VERIFICATION = 1,
  /* Verification items about a session's material, answered after a delay: see
   * speechwarp_trainer_add_retention. Not used for thresholds. */
  SPEECHWARP_MEASURE_RETENTION = 2,
  /* The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. Subjective, so
   * weighted less by default. */
  SPEECHWARP_MEASURE_RATING = 3
};

/* Session plans: how the rate moves during a session. */
enum {
  SPEECHWARP_PLAN_STEADY = 0,   /* the threshold plus a margin, all session */
  SPEECHWARP_PLAN_RAMP = 1,     /* start below the threshold and step up to threshold plus margin */
  SPEECHWARP_PLAN_INTERVAL = 2, /* alternate periods above and below the threshold */
  SPEECHWARP_PLAN_TRACKING = 3, /* move up or down after each in-session check, to stay at the target */
  SPEECHWARP_PLAN_COUNT = 4
};

/* Tunable numbers, with their defaults. Set with speechwarp_trainer_set_param. */
enum {
  SPEECHWARP_PARAM_TARGET = 0,          /* share understood that defines the threshold: 0.75 (0.5 to 0.95) */
  SPEECHWARP_PARAM_MARGIN = 1,          /* steady and ramp: aim this fraction above the threshold: 0.10;
                                           -0.5 to 1, negative to train below it */
  SPEECHWARP_PARAM_RAMP_START = 2,      /* ramp: start at this fraction of the target rate: 0.8 */
  SPEECHWARP_PARAM_RAMP_STEP = 3,       /* ramp: step by this fraction of the target rate: 0.02 */
  SPEECHWARP_PARAM_RAMP_MINUTES = 4,    /* ramp: minutes between steps: 2 */
  SPEECHWARP_PARAM_INTERVAL_SPREAD = 5, /* interval: this fraction above, then below, the threshold: 0.15 */
  SPEECHWARP_PARAM_INTERVAL_MINUTES = 6,/* interval: minutes in each period: 10 */
  SPEECHWARP_PARAM_TRACKING_GAIN = 7,   /* tracking: ln(rate) moves by gain x (score - target) per check: 0.4 */
  SPEECHWARP_PARAM_RETENTION_COST = 8,  /* plans: threshold gain a hour (as a fraction, before practice slows
                                           it) worth a whole unit of retention: 0.2, so 10 points of retention
                                           are worth 2% a hour */
  SPEECHWARP_PARAM_TEST_MAX = 9,        /* threshold test: most presentations: 40 */
  SPEECHWARP_PARAM_TEST_PRECISION = 10, /* threshold test: done when the 95% interval's high / low is below
                                           this: 1.25 */
  SPEECHWARP_PARAM_COUNT = 11
};

/* Create a trainer with a seed for its random choices. NULL if out of memory. */
SPEECHWARP_API speechwarp_trainer* speechwarp_trainer_create(uint64_t seed);
SPEECHWARP_API void speechwarp_trainer_destroy(speechwarp_trainer* trainer);

/* How much a measure of each kind counts, per item, against the others: intelligibility 0.5 (a word, and the
 * words of one sentence are not independent), verification 1, retention 1, rating 0.3 (subjective). 0 ignores
 * the kind; negative and NaN are ignored. */
SPEECHWARP_API void speechwarp_trainer_set_weight(speechwarp_trainer* trainer, int kind, double weight);
SPEECHWARP_API double speechwarp_trainer_get_weight(const speechwarp_trainer* trainer, int kind);
SPEECHWARP_API void speechwarp_trainer_set_param(speechwarp_trainer* trainer, int param, double value);
SPEECHWARP_API double speechwarp_trainer_get_param(const speechwarp_trainer* trainer, int param);

/* A score: `kind` (not RETENTION), `score` 0..1, from `items` items (for a sentence repeated back, the number of
 * words scored; for verification, the number of questions; for a rating, 1), heard at `rate` syllables a
 * second, at `time`. During a threshold
 * test it updates the estimate; during a session it is an in-session check, and the tracking plan reacts to it.
 * Returns 1, or 0 if an argument is invalid. */
SPEECHWARP_API int speechwarp_trainer_add_measure(speechwarp_trainer* trainer, int kind, double score,
                                                  double items, double rate, double time);

/* -- a. Threshold test --
 *
 * Estimates the rate understood TARGET (75%) of the time, lapses aside, by the psi method (Kontsevich and Tyler, 1999): a
 * Bayesian posterior over the threshold and the slope of a logistic psychometric function in log rate, with a
 * 4% lapse rate and each kind's guess rate (verification 0.5, others 0). Each presentation is at the rate
 * expected to tell the most about the threshold (the slope a nuisance: psi-marginal, Prins 2013). A score from
 * n items counts as n x weight Bernoulli trials. On simulated listeners a test of sentences of 8 words takes
 * about 25 presentations, lands within 3% (median), and its 95% interval holds the true threshold.
 * Prior: log-normal around `prior_rate` (sd 0.35 in ln) or, if `prior_rate` is 0, around the last estimate, or
 * failing that around 10 syllables a second (sd 0.6), within 3 to 60. */
SPEECHWARP_API void speechwarp_trainer_test_begin(speechwarp_trainer* trainer, double prior_rate, double time);
/* The rate to present next. */
SPEECHWARP_API double speechwarp_trainer_test_rate(speechwarp_trainer* trainer);
/* 1 once the 95% interval is narrower than TEST_PRECISION (at least 8 presentations) or TEST_MAX
 * presentations have been scored; 0 otherwise or if no test is running. */
SPEECHWARP_API int speechwarp_trainer_test_done(const speechwarp_trainer* trainer);
/* Finish the test; the estimate becomes the current threshold. Returns it (0 if no test was running). */
SPEECHWARP_API double speechwarp_trainer_test_end(speechwarp_trainer* trainer, double time);
/* The current estimate (posterior median) and its 95% interval: of the running test, else of the last one
 * finished; 0 if none. */
SPEECHWARP_API double speechwarp_trainer_threshold(const speechwarp_trainer* trainer);
SPEECHWARP_API double speechwarp_trainer_threshold_low(const speechwarp_trainer* trainer);
SPEECHWARP_API double speechwarp_trainer_threshold_high(const speechwarp_trainer* trainer);

/* -- b. Sessions --
 *
 * The protocol: a threshold test, session_begin, listening with a check every ~10 minutes (add_measure), a
 * second test, session_end; retention items a day and a week later (add_retention). */
SPEECHWARP_API void speechwarp_trainer_session_begin(speechwarp_trainer* trainer, int plan, double time);
/* The rate to play at now, under the session's plan, from the threshold at session_begin. 0 if no session. */
SPEECHWARP_API double speechwarp_trainer_session_rate(speechwarp_trainer* trainer, double time);
/* End the session after `listening_hours` of listening in it. It is recorded for comparing plans if a
 * threshold test ended after it began. Returns the session's number (0, 1, ...) for add_retention, or -1. */
SPEECHWARP_API int speechwarp_trainer_session_end(speechwarp_trainer* trainer, double listening_hours,
                                                  double time);
/* Retention for a recorded session: `score` 0..1 from `items` items, answered `delay_seconds` after it ended. */
SPEECHWARP_API int speechwarp_trainer_add_retention(speechwarp_trainer* trainer, int session, double score,
                                                    double items, double delay_seconds, double time);

/* -- c. Comparing plans --
 *
 * Thompson sampling over a Bayesian linear model of each recorded session's threshold gain,
 * ln(after / before) = hours x effect[plan] / (1 + hours listened by mid-session / H) + noise: gains shrink as
 * the listener improves (by H hours, to half), and allowing for that stops whichever plan ran early from
 * looking best. H is averaged over a grid from 5 hours to never, weighted by fit. A plan's sampled utility is
 * its effect less RETENTION_COST times how far its sampled retention falls below the best plan's; retention is
 * compared at like delays (a day or less, or longer). */
/* The plan to run next: a Thompson draw (advances the random source). */
SPEECHWARP_API int speechwarp_trainer_next_plan(speechwarp_trainer* trainer);
/* A plan's estimated threshold gain a hour now, at the practice so far (as a fraction: 0.01 is 1% a hour), its
 * standard deviation, its retention and standard deviation (NaN without data), sessions recorded, and the
 * probability it is the best by utility (from a fixed number of draws; does not advance the random source). */
SPEECHWARP_API double speechwarp_trainer_plan_effect(const speechwarp_trainer* trainer, int plan);
SPEECHWARP_API double speechwarp_trainer_plan_effect_sd(const speechwarp_trainer* trainer, int plan);
SPEECHWARP_API double speechwarp_trainer_plan_retention(const speechwarp_trainer* trainer, int plan);
SPEECHWARP_API double speechwarp_trainer_plan_retention_sd(const speechwarp_trainer* trainer, int plan);
SPEECHWARP_API int speechwarp_trainer_plan_sessions(const speechwarp_trainer* trainer, int plan);
SPEECHWARP_API double speechwarp_trainer_plan_best_probability(const speechwarp_trainer* trainer, int plan);
/* The trend: H, the hours of listening by which gains have halved (1000 standing for "not slowing"), and its
 * uncertainty as a standard deviation of ln H. */
SPEECHWARP_API double speechwarp_trainer_trend(const speechwarp_trainer* trainer);
SPEECHWARP_API double speechwarp_trainer_trend_sd(const speechwarp_trainer* trainer);

/* ---- d. Blind trials ------------------------------------------------------------------------------------
 *
 * Designing the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
 * values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
 * caller names the settings; here they are numbers. Deterministic for a given seed. */

typedef struct speechwarp_trials speechwarp_trials;

SPEECHWARP_API speechwarp_trials* speechwarp_trials_create(uint64_t seed);
SPEECHWARP_API void speechwarp_trials_destroy(speechwarp_trials* trials);
/* Add a setting; returns its number (0, 1, ...), or -1. */
SPEECHWARP_API int speechwarp_trials_add_setting(speechwarp_trials* trials);
/* Add a value to compare; returns its number within the setting, or -1 (duplicate, bad setting). */
SPEECHWARP_API int speechwarp_trials_add_value(speechwarp_trials* trials, int setting, double value);
/* Leave a setting out of next_trial while 0 (say, when its method is not available). Default 1. */
SPEECHWARP_API void speechwarp_trials_set_available(speechwarp_trials* trials, int setting, int available);
/* Record a trial at `speed`: the two values in the order heard, each one's score 0..1, and `preferred`: -1 the
 * first, 1 the second, 0 neither. Values must be ones added. Returns 1, or 0. */
SPEECHWARP_API int speechwarp_trials_add(speechwarp_trials* trials, int setting, double speed,
                                         double first_value, double second_value, double first_score,
                                         double second_score, int preferred);
/* Choose the next trial at `speed`: the available setting with the fewest trials in its band (ties at random),
 * its pair of values compared least (ties at random), in random order. Returns the setting, or -1 if none has
 * two values; next_first and next_second then give the values. */
SPEECHWARP_API int speechwarp_trials_next(speechwarp_trials* trials, double speed);
SPEECHWARP_API double speechwarp_trials_next_first(const speechwarp_trials* trials);
SPEECHWARP_API double speechwarp_trials_next_second(const speechwarp_trials* trials);
/* Results for one value of a setting in the band of `speed`: comparisons won, lost and tied, trials heard in,
 * and its mean score (NaN if never heard). */
SPEECHWARP_API int speechwarp_trials_won(const speechwarp_trials* trials, int setting, double speed, int value);
SPEECHWARP_API int speechwarp_trials_lost(const speechwarp_trials* trials, int setting, double speed, int value);
SPEECHWARP_API int speechwarp_trials_tied(const speechwarp_trials* trials, int setting, double speed, int value);
SPEECHWARP_API int speechwarp_trials_heard(const speechwarp_trials* trials, int setting, double speed, int value);
SPEECHWARP_API double speechwarp_trials_mean_score(const speechwarp_trials* trials, int setting, double speed,
                                                   int value);
/* The value with a reliable win in that band, or -1. A value wins when it has been heard in at least 5 trials,
 * has met every other value in at least 3, and against each the Bayes factor for "preferred" (preference
 * uniform on 1/2..1) over "no preference" (ties counting half each way) is at least 1 / (1 - confidence): 20
 * at the default 0.95. However often this is asked, the chance of ever naming a winner between two values
 * that are really alike is at most 1 - confidence each way (Ville's inequality). */
SPEECHWARP_API int speechwarp_trials_winner(const speechwarp_trials* trials, int setting, double speed);
SPEECHWARP_API void speechwarp_trials_set_confidence(speechwarp_trials* trials, double confidence);

#ifdef __cplusplus
}
#endif

#endif  /* SPEECHWARP_H_ */
