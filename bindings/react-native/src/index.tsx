/**
 * speechwarp - nonlinear speed-up for speech, for React Native.
 *
 *     import { Stream } from 'react-native-speechwarp';
 *
 *     const stream = new Stream(44100, 2);
 *     stream.speed = 3;
 *     stream.write(samples);            // interleaved Float32Array
 *     const out = stream.read();
 *     stream.flush();                   // at the end of the input
 *     stream.free();
 *
 * Audio is Float32Array in the range -1 to 1, interleaved: a frame is one sample per channel. Counts are in
 * frames. The calls are synchronous and run on the JavaScript thread.
 */
import Native from './NativeSpeechwarp';

/** The slowest speed that can be set. */
export const MIN_SPEED = 0.05;
/** The fastest speed that can be set. */
export const MAX_SPEED = 20;

/** The version of the native library, such as "0.3.5". */
export function version(): string {
  return Native.version();
}

/** Speeds up speech. Write audio in, read the faster audio out. */
function number(name: string, value: number): number {
  if (Number.isNaN(value)) {
    throw new RangeError(`${name} must be a number`);
  }
  return value;
}

export class Stream {
  readonly sampleRate: number;
  readonly channels: number;
  private handle: number;

  /**
   * Create a stream at speed 1 with nonlinear speed-up on.
   * @param sampleRate Samples per second, 4000 to 384000.
   * @param channels 1 to 32.
   */
  constructor(sampleRate: number, channels = 1) {
    if (!Number.isInteger(sampleRate) || sampleRate < 4000 || sampleRate > 384000) {
      throw new RangeError(`sampleRate must be a whole number from 4000 to 384000, not ${sampleRate}`);
    }
    if (!Number.isInteger(channels) || channels < 1 || channels > 32) {
      throw new RangeError(`channels must be a whole number from 1 to 32, not ${channels}`);
    }
    this.handle = Native.createStream(sampleRate, channels);
    if (this.handle === 0) {
      throw new Error('speechwarp: out of memory');
    }
    this.sampleRate = sampleRate;
    this.channels = channels;
  }

  /**
   * Overall speed: 2 plays twice as fast. Clamped to MIN_SPEED..MAX_SPEED.
   *
   * Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
   * speed-up the speed varies from moment to moment and its average is steered to this value; expect the
   * result within a few percent.
   */
  get speed(): number {
    return Native.getSpeed(this.open());
  }

  set speed(value: number) {
    if (!(value > 0)) {
      throw new RangeError(`speed must be greater than zero, not ${value}`);
    }
    Native.setSpeed(this.open(), value);
  }

  /**
   * How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and pauses,
   * as a fast talker does. 0 compresses everything evenly. May be changed during playback.
   */
  get nonlinear(): number {
    return Native.getNonlinear(this.open());
  }

  set nonlinear(value: number) {
    if (Number.isNaN(value)) {
      throw new RangeError('nonlinear must be a number');
    }
    Native.setNonlinear(this.open(), value);
  }

  // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
  // docs/how-it-works.md.

  /**
   * Shorten every pause to at most this many seconds of input before speeding up, so that the speed is spent
   * on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. `position` counts the
   * frames left out.
   */
  get pauseCap(): number {
    return Native.getPauseCap(this.open());
  }

  set pauseCap(value: number) {
    Native.setPauseCap(this.open(), number('pauseCap', value));
  }

  /**
   * While the pause cap or rhythm is on, keep the overall speed (true, the default): time saved in pauses is
   * spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
   * When false, trimmed pauses make playback faster than `speed`.
   */
  get keepSpeed(): boolean {
    return Native.getKeepSpeed(this.open());
  }

  set keepSpeed(value: boolean) {
    Native.setKeepSpeed(this.open(), value);
  }

  /**
   * No 10 ms block of speech plays slower than this fraction of `speed`: at 8x, 0.5 keeps every block at 4x
   * or more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with
   * nonlinear speed-up above 1x.
   */
  get speedFloor(): number {
    return Native.getSpeedFloor(this.open());
  }

  set speedFloor(value: number) {
    Native.setSpeedFloor(this.open(), number('speedFloor', value));
  }

  /**
   * Seconds of silence put into the output `rhythmRate` times a second, at the quietest point nearby, which
   * can help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06.
   * Allowed: 0, or 0.005 to 0.2. `position` holds still during a gap.
   */
  get rhythmGap(): number {
    return Native.getRhythmGap(this.open());
  }

  set rhythmGap(value: number) {
    Native.setRhythmGap(this.open(), number('rhythmGap', value));
  }

  /** Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. */
  get rhythmRate(): number {
    return Native.getRhythmRate(this.open());
  }

  set rhythmRate(value: number) {
    if (!(value > 0)) {
      throw new RangeError(`rhythmRate must be greater than zero, not ${value}`);
    }
    Native.setRhythmRate(this.open(), value);
  }

  // Options that follow the speed. See docs/how-it-works.md.

  /**
   * Keep each pause about `seconds` long in the output: the pause cap in force is `seconds` times the current
   * speed, clamped to 0.03 to 0.4 s of input, and is applied whenever `speed` changes. Below `fromSpeed`
   * pauses are left alone. `seconds`: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to
   * 0.4. `fromSpeed`: 1 to 20. Sensible: 0.015 to 0.06 from 3x. Setting `pauseCap` turns the rule off.
   */
  setHeardPause(seconds: number, fromSpeed: number): void {
    Native.setHeardPause(this.open(), number('seconds', seconds), number('fromSpeed', fromSpeed));
  }

  /** The `seconds` last given to `setHeardPause`; 0 while the rule is off. */
  get heardPause(): number {
    return Native.getHeardPause(this.open());
  }

  /** The `fromSpeed` last given to `setHeardPause`. */
  get heardPauseFrom(): number {
    return Native.getHeardPauseFrom(this.open());
  }

  /**
   * The speed floor in force is 0 below `fromSpeed`, rises linearly to `fraction` at `fullSpeed` and stays
   * there above it, so that a speed ramp never changes the sound in a jump. `fraction`: 0 turns the rule off
   * (and the floor with it); otherwise up to 1. Speeds 1 to 20; if `fullSpeed` is not above `fromSpeed` the
   * floor steps to `fraction` at `fromSpeed`. Sensible: 0.5 from 4x, full at 6x. Setting `speedFloor` turns
   * the rule off.
   */
  setFloorBlend(fraction: number, fromSpeed: number, fullSpeed: number): void {
    Native.setFloorBlend(
      this.open(),
      number('fraction', fraction),
      number('fromSpeed', fromSpeed),
      number('fullSpeed', fullSpeed)
    );
  }

  /** The `fraction` last given to `setFloorBlend`; 0 while the rule is off. */
  get floorBlend(): number {
    return Native.getFloorBlend(this.open());
  }

  /** The `fromSpeed` last given to `setFloorBlend`. */
  get floorBlendFrom(): number {
    return Native.getFloorBlendFrom(this.open());
  }

  /** The `fullSpeed` last given to `setFloorBlend`. */
  get floorBlendFull(): number {
    return Native.getFloorBlendFull(this.open());
  }

  /**
   * Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
   * been written since creation or `reset`. Multiply by the speed for the rate heard. An estimate, typically
   * within about 10%.
   */
  get syllableRate(): number | null {
    const rate = Native.syllableRate(this.open());
    return rate < 0 ? null : rate;
  }

  /** Frames of output ready to read. */
  get available(): number {
    return Native.available(this.open());
  }

  /**
   * The input frame, counted from creation or the last `reset`, that the next output frame to be read was
   * made from. This is how a player maps what is being heard back to a place in the source.
   *
   * It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
   * `flush` has been read it equals the number of frames written.
   */
  get position(): number {
    return Native.position(this.open());
  }

  /** Add interleaved input. The output does not depend on how the input is divided between calls. */
  write(samples: Float32Array): void {
    if (samples.length % this.channels !== 0) {
      throw new RangeError(`${samples.length} samples is not a whole number of ${this.channels}-channel frames`);
    }
    const frames = samples.length / this.channels;
    if (frames > 0 && !Native.write(this.open(), samples.buffer, samples.byteOffset, frames)) {
      throw new Error('speechwarp: out of memory');
    }
  }

  /**
   * Take interleaved output.
   *
   * Given an array, fills as many whole frames of it as it can and returns the number of frames. Given a
   * number or nothing, returns a new array of at most that many frames, or of everything ready. Either may
   * come back empty: output lags input by a short look-ahead.
   */
  read(into: Float32Array): number;
  read(maxFrames?: number): Float32Array;
  read(target?: Float32Array | number): number | Float32Array {
    const handle = this.open();
    if (target instanceof Float32Array) {
      const room = Math.floor(target.length / this.channels);
      return room > 0 ? Native.read(handle, target.buffer, target.byteOffset, room) : 0;
    }
    const frames = Math.max(0, Math.min(target ?? Infinity, Native.available(handle)));
    const out = new Float32Array(frames * this.channels);
    if (frames === 0) {
      return out;
    }
    const got = Native.read(handle, out.buffer, 0, frames);
    return got === frames ? out : out.subarray(0, got * this.channels);
  }

  /**
   * Process everything written so far, at the end of the input. Read until empty afterwards. Writing more
   * starts a new stretch of audio, and `position` carries on counting.
   */
  flush(): void {
    if (!Native.flush(this.open())) {
      throw new Error('speechwarp: out of memory');
    }
  }

  /**
   * Discard all buffered input and output, keeping the speed and nonlinear settings, and start `position`
   * again from zero. Use after seeking.
   */
  reset(): void {
    Native.reset(this.open());
  }

  /**
   * Release the native stream. Using the stream afterwards throws. A stream that is never freed stays in
   * memory until the app exits, so call this.
   */
  free(): void {
    if (this.handle !== 0) {
      Native.destroyStream(this.handle);
      this.handle = 0;
    }
  }

  private open(): number {
    if (this.handle === 0) {
      throw new Error('speechwarp: the stream has been freed');
    }
    return this.handle;
  }
}

function seedNumber(seed: number): number {
  if (!Number.isSafeInteger(seed) || seed < 0) {
    throw new RangeError(`seed must be a whole number from 0 to 2^53 - 1, not ${seed}`);
  }
  return seed;
}

/**
 * The syllable counter of a stream, on its own: for audio that does not go through a stream (a player using
 * some other speed-up, or measuring a file). A counter given the same input as a stream reports the same rate
 * as `Stream.syllableRate`.
 */
export class SyllableCounter {
  readonly sampleRate: number;
  readonly channels: number;
  private handle: number;

  /**
   * @param sampleRate Samples per second, 4000 to 384000.
   * @param channels 1 to 32.
   */
  constructor(sampleRate: number, channels = 1) {
    if (!Number.isInteger(sampleRate) || sampleRate < 4000 || sampleRate > 384000) {
      throw new RangeError(`sampleRate must be a whole number from 4000 to 384000, not ${sampleRate}`);
    }
    if (!Number.isInteger(channels) || channels < 1 || channels > 32) {
      throw new RangeError(`channels must be a whole number from 1 to 32, not ${channels}`);
    }
    this.handle = Native.syllablesCreate(sampleRate, channels);
    if (this.handle === 0) {
      throw new Error('speechwarp: out of memory');
    }
    this.sampleRate = sampleRate;
    this.channels = channels;
  }

  /** Add interleaved samples: floats in the range -1 to 1, or 16-bit integers. */
  write(samples: Float32Array | Int16Array): void {
    const handle = this.open();
    if (samples.length % this.channels !== 0) {
      throw new RangeError(`${samples.length} samples is not a whole number of ${this.channels}-channel frames`);
    }
    const frames = samples.length / this.channels;
    if (frames === 0) {
      return;
    }
    const ok =
      samples instanceof Int16Array
        ? Native.syllablesWriteI16(handle, samples.buffer, samples.byteOffset, frames)
        : Native.syllablesWrite(handle, samples.buffer, samples.byteOffset, frames);
    if (!ok) {
      throw new Error('speechwarp: the samples could not be counted');
    }
  }

  /**
   * Syllables a second over the last `windowSeconds` written (or all of it, if less); null until
   * `minimumSeconds` have been written. The window is clamped to 1 to 120 s, the minimum to 0 to the window.
   */
  rate(windowSeconds = 60, minimumSeconds = 10): number | null {
    const rate = Native.syllablesRate(
      this.open(),
      number('windowSeconds', windowSeconds),
      number('minimumSeconds', minimumSeconds)
    );
    return rate < 0 ? null : rate;
  }

  /** Forget everything written. */
  reset(): void {
    Native.syllablesReset(this.open());
  }

  /** Release the native counter. Using it afterwards throws. */
  free(): void {
    if (this.handle !== 0) {
      Native.syllablesDestroy(this.handle);
      this.handle = 0;
    }
  }

  private open(): number {
    if (this.handle === 0) {
      throw new Error('speechwarp: the syllable counter has been freed');
    }
    return this.handle;
  }
}

/** What a score in 0..1 measures. */
export enum TrainerMeasure {
  /** Share of the words said back correctly from a sentence heard once. */
  Intelligibility = 0,
  /** Share right on "was this sentence in what you just heard?" items. Chance is 0.5. */
  Verification = 1,
  /** Verification items about a session's material, answered after a delay; see `addRetention`. */
  Retention = 2,
  /** The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. */
  Rating = 3,
}

/** Session plans: how the rate moves during a session. */
export enum TrainerPlan {
  /** The threshold plus a margin, all session. */
  Steady = 0,
  /** Start below the threshold and step up to threshold plus margin. */
  Ramp = 1,
  /** Alternate periods above and below the threshold. */
  Interval = 2,
  /** Move up or down after each in-session check, to stay at the target. */
  Tracking = 3,
}

/** Tunable numbers of the trainer, with their defaults. */
export enum TrainerParam {
  /** Share understood that defines the threshold: 0.75 (0.5 to 0.95). */
  Target = 0,
  /** Steady and ramp: aim this fraction above the threshold: 0.10. */
  Margin = 1,
  /** Ramp: start at this fraction of the target rate: 0.8. */
  RampStart = 2,
  /** Ramp: step by this fraction of the target rate: 0.02. */
  RampStep = 3,
  /** Ramp: minutes between steps: 2. */
  RampMinutes = 4,
  /** Interval: this fraction above, then below, the threshold: 0.15. */
  IntervalSpread = 5,
  /** Interval: minutes in each period: 10. */
  IntervalMinutes = 6,
  /** Tracking: ln(rate) moves by gain x (score - target) per check: 0.4. */
  TrackingGain = 7,
  /** Plans: threshold gain an hour worth a whole unit of retention: 0.2. */
  RetentionCost = 8,
  /** Threshold test: most presentations: 40. */
  TestMax = 9,
  /** Threshold test: done when the 95% interval's high / low is below this: 1.25. */
  TestPrecision = 10,
}

/**
 * Training a listener to follow faster speech: pure logic with no audio, clock or storage. Scores, rates and
 * timestamps go in as plain numbers; rates and plans come out. Deterministic: two trainers with the same seed
 * given the same calls give the same answers, so keep a log of calls and replay it to restore state.
 *
 * The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times
 * are seconds on any clock, and only differences are used. NaN arguments are ignored.
 */
export class ListenerTrainer {
  private handle: number;

  /** @param seed Seed for the trainer's random choices: a whole number from 0 to 2^53 - 1. */
  constructor(seed = 0) {
    this.handle = Native.trainerCreate(seedNumber(seed));
    if (this.handle === 0) {
      throw new Error('speechwarp: out of memory');
    }
  }

  /**
   * How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification
   * 1, retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
   */
  setWeight(kind: TrainerMeasure, weight: number): void {
    Native.trainerSetWeight(this.open(), kind, weight);
  }

  getWeight(kind: TrainerMeasure): number {
    return Native.trainerGetWeight(this.open(), kind);
  }

  /** Set a tunable number; see `TrainerParam` for the defaults. */
  setParam(param: TrainerParam, value: number): void {
    Native.trainerSetParam(this.open(), param, value);
  }

  getParam(param: TrainerParam): number {
    return Native.trainerGetParam(this.open(), param);
  }

  /**
   * Record a score: `kind` (not Retention), `score` 0..1, from `items` items (for a sentence repeated back,
   * the number of words scored; for verification, the number of questions; for a rating, 1), heard at `rate`
   * syllables a second, at `time`. During a threshold test it updates the estimate; during a session it is
   * an in-session check, and the tracking plan reacts to it. Returns false if an argument is invalid.
   */
  addMeasure(kind: TrainerMeasure, score: number, items: number, rate: number, time: number): boolean {
    return Native.trainerAddMeasure(this.open(), kind, score, items, rate, time);
  }

  /**
   * Start a threshold test: estimates the rate understood `TrainerParam.Target` (75%) of the time, by the psi
   * method. The prior is log-normal around `priorRate` or, if that is 0, around the last estimate, or failing
   * that around 10 syllables a second, within 3 to 60.
   */
  testBegin(priorRate: number, time: number): void {
    Native.trainerTestBegin(this.open(), priorRate, time);
  }

  /** The rate to present next. */
  testRate(): number {
    return Native.trainerTestRate(this.open());
  }

  /**
   * True once the 95% interval is narrower than `TrainerParam.TestPrecision` (at least 8 presentations) or
   * `TrainerParam.TestMax` presentations have been scored; false otherwise or if no test is running.
   */
  get testDone(): boolean {
    return Native.trainerTestDone(this.open());
  }

  /** Finish the test; the estimate becomes the current threshold. Returns it (0 if no test was running). */
  testEnd(time: number): number {
    return Native.trainerTestEnd(this.open(), time);
  }

  /** The current estimate (posterior median): of the running test, else of the last one finished; 0 if none. */
  get threshold(): number {
    return Native.trainerThreshold(this.open());
  }

  /** The low end of the 95% interval of the estimate. */
  get thresholdLow(): number {
    return Native.trainerThresholdLow(this.open());
  }

  /** The high end of the 95% interval of the estimate. */
  get thresholdHigh(): number {
    return Native.trainerThresholdHigh(this.open());
  }

  /** Begin a session under `plan`. */
  sessionBegin(plan: TrainerPlan, time: number): void {
    Native.trainerSessionBegin(this.open(), plan, time);
  }

  /** The rate to play at now, under the session's plan, from the threshold at `sessionBegin`. 0 if no session. */
  sessionRate(time: number): number {
    return Native.trainerSessionRate(this.open(), time);
  }

  /**
   * End the session after `listeningHours` of listening in it. It is recorded for comparing plans if a
   * threshold test ended after it began. Returns the session's number (0, 1, ...) for `addRetention`, or -1.
   */
  sessionEnd(listeningHours: number, time: number): number {
    return Native.trainerSessionEnd(this.open(), listeningHours, time);
  }

  /**
   * Retention for a recorded session: `score` 0..1 from `items` items, answered `delaySeconds` after it
   * ended. Returns false if an argument is invalid.
   */
  addRetention(session: number, score: number, items: number, delaySeconds: number, time: number): boolean {
    return Native.trainerAddRetention(this.open(), session, score, items, delaySeconds, time);
  }

  /** The plan to run next: a Thompson draw (advances the random source). */
  nextPlan(): TrainerPlan {
    return Native.trainerNextPlan(this.open());
  }

  /** A plan's estimated threshold gain an hour now, as a fraction: 0.01 is 1% an hour. */
  planEffect(plan: TrainerPlan): number {
    return Native.trainerPlanEffect(this.open(), plan);
  }

  /** The standard deviation of `planEffect`. */
  planEffectSd(plan: TrainerPlan): number {
    return Native.trainerPlanEffectSd(this.open(), plan);
  }

  /** A plan's retention; NaN without data. */
  planRetention(plan: TrainerPlan): number {
    return Native.trainerPlanRetention(this.open(), plan);
  }

  /** The standard deviation of `planRetention`; NaN without data. */
  planRetentionSd(plan: TrainerPlan): number {
    return Native.trainerPlanRetentionSd(this.open(), plan);
  }

  /** Sessions recorded under a plan. */
  planSessions(plan: TrainerPlan): number {
    return Native.trainerPlanSessions(this.open(), plan);
  }

  /** The probability that a plan is the best by utility (does not advance the random source). */
  planBestProbability(plan: TrainerPlan): number {
    return Native.trainerPlanBestProbability(this.open(), plan);
  }

  /** H, the hours of listening by which gains have halved (1000 standing for "not slowing"). */
  get trend(): number {
    return Native.trainerTrend(this.open());
  }

  /** The uncertainty of `trend` as a standard deviation of ln H. */
  get trendSd(): number {
    return Native.trainerTrendSd(this.open());
  }

  /** Release the native trainer. Using it afterwards throws. */
  free(): void {
    if (this.handle !== 0) {
      Native.trainerDestroy(this.handle);
      this.handle = 0;
    }
  }

  private open(): number {
    if (this.handle === 0) {
      throw new Error('speechwarp: the trainer has been freed');
    }
    return this.handle;
  }
}

/**
 * Designing the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of
 * its values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...).
 * The caller names the settings; here they are numbers. Deterministic for a given seed.
 */
export class BlindTrials {
  private handle: number;

  /** @param seed Seed for the random choices: a whole number from 0 to 2^53 - 1. */
  constructor(seed = 0) {
    this.handle = Native.trialsCreate(seedNumber(seed));
    if (this.handle === 0) {
      throw new Error('speechwarp: out of memory');
    }
  }

  /** Add a setting; returns its number (0, 1, ...), or -1. */
  addSetting(): number {
    return Native.trialsAddSetting(this.open());
  }

  /** Add a value to compare; returns its number within the setting, or -1 (duplicate, bad setting). */
  addValue(setting: number, value: number): number {
    return Native.trialsAddValue(this.open(), setting, value);
  }

  /** Leave a setting out of `next` while false (say, when its method is not available). Default true. */
  setAvailable(setting: number, available: boolean): void {
    Native.trialsSetAvailable(this.open(), setting, available);
  }

  /**
   * Record a trial at `speed`: the two values in the order heard, each one's score 0..1, and `preferred`: -1
   * the first, 1 the second, 0 neither. Values must be ones added. Returns false if an argument is invalid.
   */
  add(
    setting: number,
    speed: number,
    firstValue: number,
    secondValue: number,
    firstScore: number,
    secondScore: number,
    preferred: number
  ): boolean {
    return Native.trialsAdd(
      this.open(),
      setting,
      speed,
      firstValue,
      secondValue,
      firstScore,
      secondScore,
      preferred
    );
  }

  /**
   * Choose the next trial at `speed`: the available setting with the fewest trials in its band (ties at
   * random), its pair of values compared least (ties at random), in random order. Null if no setting has two
   * values.
   */
  next(speed: number): { setting: number; first: number; second: number } | null {
    const handle = this.open();
    const setting = Native.trialsNext(handle, speed);
    if (setting < 0) {
      return null;
    }
    return { setting, first: Native.trialsNextFirst(handle), second: Native.trialsNextSecond(handle) };
  }

  /** Comparisons won by one value of a setting in the band of `speed`. */
  won(setting: number, speed: number, value: number): number {
    return Native.trialsWon(this.open(), setting, speed, value);
  }

  /** Comparisons lost by one value of a setting in the band of `speed`. */
  lost(setting: number, speed: number, value: number): number {
    return Native.trialsLost(this.open(), setting, speed, value);
  }

  /** Comparisons tied by one value of a setting in the band of `speed`. */
  tied(setting: number, speed: number, value: number): number {
    return Native.trialsTied(this.open(), setting, speed, value);
  }

  /** Trials one value of a setting was heard in, in the band of `speed`. */
  heard(setting: number, speed: number, value: number): number {
    return Native.trialsHeard(this.open(), setting, speed, value);
  }

  /** The mean score of one value in the band of `speed`; null if never heard. */
  meanScore(setting: number, speed: number, value: number): number | null {
    const score = Native.trialsMeanScore(this.open(), setting, speed, value);
    return Number.isNaN(score) ? null : score;
  }

  /**
   * The value with a reliable win in that band, or null. A value wins when it has been heard in at least 5
   * trials, has met every other value in at least 3, and against each the Bayes factor for "preferred" over
   * "no preference" is at least 1 / (1 - confidence): 20 at the default 0.95.
   */
  winner(setting: number, speed: number): number | null {
    const value = Native.trialsWinner(this.open(), setting, speed);
    return value < 0 ? null : value;
  }

  /** The confidence `winner` demands, 0 to 1; default 0.95. */
  setConfidence(confidence: number): void {
    Native.trialsSetConfidence(this.open(), confidence);
  }

  /** Release the native trials. Using them afterwards throws. */
  free(): void {
    if (this.handle !== 0) {
      Native.trialsDestroy(this.handle);
      this.handle = 0;
    }
  }

  private open(): number {
    if (this.handle === 0) {
      throw new Error('speechwarp: the trials have been freed');
    }
    return this.handle;
  }
}
