/**
 * speechwarp - nonlinear speed-up for speech.
 *
 *     import { load } from "speechwarp";
 *     const speechwarp = await load();
 *     const stream = speechwarp.createStream(44100, 2);
 *     stream.speed = 3;
 *     stream.write(samples);            // interleaved Float32Array
 *     const out = stream.read();
 *
 * Licensed under the Apache License, Version 2.0.
 */
import { wasmBase64 } from "./wasm.js";

/** The slowest speed that can be set. */
export const MIN_SPEED = 0.05;
/** The fastest speed that can be set. */
export const MAX_SPEED = 20;

interface Exports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  malloc(bytes: number): number;
  free(pointer: number): void;
  speechwarp_version(): number;
  speechwarp_create(sampleRate: number, channels: number): number;
  speechwarp_destroy(stream: number): void;
  speechwarp_set_speed(stream: number, speed: number): void;
  speechwarp_get_speed(stream: number): number;
  speechwarp_set_nonlinear(stream: number, amount: number): void;
  speechwarp_get_nonlinear(stream: number): number;
  speechwarp_set_pause_cap(stream: number, seconds: number): void;
  speechwarp_get_pause_cap(stream: number): number;
  speechwarp_set_keep_speed(stream: number, enabled: number): void;
  speechwarp_get_keep_speed(stream: number): number;
  speechwarp_set_speed_floor(stream: number, fraction: number): void;
  speechwarp_get_speed_floor(stream: number): number;
  speechwarp_set_rhythm_gap(stream: number, seconds: number): void;
  speechwarp_get_rhythm_gap(stream: number): number;
  speechwarp_set_rhythm_rate(stream: number, perSecond: number): void;
  speechwarp_get_rhythm_rate(stream: number): number;
  speechwarp_syllable_rate(stream: number): number;
  speechwarp_set_heard_pause(stream: number, seconds: number, fromSpeed: number): void;
  speechwarp_get_heard_pause(stream: number): number;
  speechwarp_get_heard_pause_from(stream: number): number;
  speechwarp_set_floor_blend(stream: number, fraction: number, fromSpeed: number, fullSpeed: number): void;
  speechwarp_get_floor_blend(stream: number): number;
  speechwarp_get_floor_blend_from(stream: number): number;
  speechwarp_get_floor_blend_full(stream: number): number;
  speechwarp_syllables_create(sampleRate: number, channels: number): number;
  speechwarp_syllables_destroy(counter: number): void;
  speechwarp_syllables_write(counter: number, samples: number, frames: number): number;
  speechwarp_syllables_write_i16(counter: number, samples: number, frames: number): number;
  speechwarp_syllables_rate(counter: number, windowSeconds: number, minimumSeconds: number): number;
  speechwarp_syllables_reset(counter: number): void;
  speechwarp_trainer_create(seed: bigint): number;
  speechwarp_trainer_destroy(trainer: number): void;
  speechwarp_trainer_set_weight(trainer: number, kind: number, weight: number): void;
  speechwarp_trainer_get_weight(trainer: number, kind: number): number;
  speechwarp_trainer_set_param(trainer: number, param: number, value: number): void;
  speechwarp_trainer_get_param(trainer: number, param: number): number;
  speechwarp_trainer_add_measure(
    trainer: number, kind: number, score: number, items: number, rate: number, time: number,
  ): number;
  speechwarp_trainer_test_begin(trainer: number, priorRate: number, time: number): void;
  speechwarp_trainer_test_rate(trainer: number): number;
  speechwarp_trainer_test_done(trainer: number): number;
  speechwarp_trainer_test_end(trainer: number, time: number): number;
  speechwarp_trainer_threshold(trainer: number): number;
  speechwarp_trainer_threshold_low(trainer: number): number;
  speechwarp_trainer_threshold_high(trainer: number): number;
  speechwarp_trainer_session_begin(trainer: number, plan: number, time: number): void;
  speechwarp_trainer_session_rate(trainer: number, time: number): number;
  speechwarp_trainer_session_end(trainer: number, listeningHours: number, time: number): number;
  speechwarp_trainer_add_retention(
    trainer: number, session: number, score: number, items: number, delaySeconds: number, time: number,
  ): number;
  speechwarp_trainer_next_plan(trainer: number): number;
  speechwarp_trainer_plan_effect(trainer: number, plan: number): number;
  speechwarp_trainer_plan_effect_sd(trainer: number, plan: number): number;
  speechwarp_trainer_plan_retention(trainer: number, plan: number): number;
  speechwarp_trainer_plan_retention_sd(trainer: number, plan: number): number;
  speechwarp_trainer_plan_sessions(trainer: number, plan: number): number;
  speechwarp_trainer_plan_best_probability(trainer: number, plan: number): number;
  speechwarp_trainer_trend(trainer: number): number;
  speechwarp_trainer_trend_sd(trainer: number): number;
  speechwarp_trials_create(seed: bigint): number;
  speechwarp_trials_destroy(trials: number): void;
  speechwarp_trials_add_setting(trials: number): number;
  speechwarp_trials_add_value(trials: number, setting: number, value: number): number;
  speechwarp_trials_set_available(trials: number, setting: number, available: number): void;
  speechwarp_trials_add(
    trials: number, setting: number, speed: number, firstValue: number, secondValue: number,
    firstScore: number, secondScore: number, preferred: number,
  ): number;
  speechwarp_trials_next(trials: number, speed: number): number;
  speechwarp_trials_next_first(trials: number): number;
  speechwarp_trials_next_second(trials: number): number;
  speechwarp_trials_won(trials: number, setting: number, speed: number, value: number): number;
  speechwarp_trials_lost(trials: number, setting: number, speed: number, value: number): number;
  speechwarp_trials_tied(trials: number, setting: number, speed: number, value: number): number;
  speechwarp_trials_heard(trials: number, setting: number, speed: number, value: number): number;
  speechwarp_trials_mean_score(trials: number, setting: number, speed: number, value: number): number;
  speechwarp_trials_winner(trials: number, setting: number, speed: number): number;
  speechwarp_trials_set_confidence(trials: number, confidence: number): void;
  speechwarp_write(stream: number, samples: number, frames: number): number;
  speechwarp_read(stream: number, samples: number, maxFrames: number): number;
  speechwarp_available(stream: number): number;
  speechwarp_flush(stream: number): number;
  speechwarp_reset(stream: number): void;
  speechwarp_position(stream: number): bigint;
}

function number(name: string, value: number): number {
  if (Number.isNaN(value)) throw new RangeError(`${name} must be a number`);
  return value;
}

/** atob does not exist in an AudioWorklet, so this does without it. */
function decodeBase64(text: string): ArrayBuffer {
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const lookup = new Uint8Array(128);
  for (let i = 0; i < 64; i++) lookup[alphabet.charCodeAt(i)] = i;
  const padding = text.endsWith("==") ? 2 : text.endsWith("=") ? 1 : 0;
  const bytes = new Uint8Array((text.length / 4) * 3 - padding);
  let out = 0;
  for (let i = 0; i < text.length; i += 4) {
    const bits =
      (lookup[text.charCodeAt(i)] << 18) |
      (lookup[text.charCodeAt(i + 1)] << 12) |
      (lookup[text.charCodeAt(i + 2)] << 6) |
      lookup[text.charCodeAt(i + 3)];
    if (out < bytes.length) bytes[out++] = bits >> 16;
    if (out < bytes.length) bytes[out++] = (bits >> 8) & 255;
    if (out < bytes.length) bytes[out++] = bits & 255;
  }
  return bytes.buffer;
}

/** The C library asks its host for a few things it never uses in earnest, such as somewhere to print. */
function importsFor(module: WebAssembly.Module): WebAssembly.Imports {
  const imports: Record<string, Record<string, WebAssembly.ImportValue>> = {};
  for (const { module: from, name, kind } of WebAssembly.Module.imports(module)) {
    if (kind !== "function") throw new Error(`speechwarp: unexpected import ${from}.${name}`);
    (imports[from] ??= {})[name] = () => 0;
  }
  return imports;
}

function start(instance: WebAssembly.Instance): Speechwarp {
  const exports = instance.exports as unknown as Exports;
  exports._initialize?.();
  return new Speechwarp(exports);
}

let loading: Promise<Speechwarp> | undefined;

/** Get the library ready. The result is shared: calling this again does not load it twice. */
export function load(): Promise<Speechwarp> {
  return (loading ??= (async () => {
    const module = await WebAssembly.compile(decodeBase64(wasmBase64));
    return start(await WebAssembly.instantiate(module, importsFor(module)));
  })());
}

/**
 * Get the library ready without waiting. Use this where `await` is not possible, such as the constructor of
 * an AudioWorkletProcessor. Browsers refuse to compile this much WebAssembly synchronously on the main
 * thread; use `load` there.
 */
export function loadSync(): Speechwarp {
  const module = new WebAssembly.Module(decodeBase64(wasmBase64));
  return start(new WebAssembly.Instance(module, importsFor(module)));
}

/** The loaded library. Get one from `load` or `loadSync`. */
export class Speechwarp {
  /** The version of the C library, such as "0.3.6". */
  readonly version: string;

  /** @internal */
  constructor(private readonly exports: Exports) {
    const bytes = new Uint8Array(exports.memory.buffer);
    let text = "";
    for (let at = exports.speechwarp_version(); bytes[at] !== 0; at++) text += String.fromCharCode(bytes[at]);
    this.version = text;
  }

  /**
   * Create a stream at speed 1 with nonlinear speed-up on.
   * @param sampleRate Samples per second, 4000 to 384000.
   * @param channels 1 to 32.
   */
  createStream(sampleRate: number, channels = 1): Stream {
    if (!Number.isInteger(sampleRate) || sampleRate < 4000 || sampleRate > 384000) {
      throw new RangeError(`sampleRate must be a whole number from 4000 to 384000, not ${sampleRate}`);
    }
    if (!Number.isInteger(channels) || channels < 1 || channels > 32) {
      throw new RangeError(`channels must be a whole number from 1 to 32, not ${channels}`);
    }
    return new Stream(this.exports, sampleRate, channels);
  }

  /**
   * Create a syllable counter: the estimator behind `Stream.syllableRate`, for audio that does not go through
   * a stream.
   * @param sampleRate Samples per second, 4000 to 384000.
   * @param channels 1 to 32.
   */
  createSyllableCounter(sampleRate: number, channels = 1): SyllableCounter {
    if (!Number.isInteger(sampleRate) || sampleRate < 4000 || sampleRate > 384000) {
      throw new RangeError(`sampleRate must be a whole number from 4000 to 384000, not ${sampleRate}`);
    }
    if (!Number.isInteger(channels) || channels < 1 || channels > 32) {
      throw new RangeError(`channels must be a whole number from 1 to 32, not ${channels}`);
    }
    return new SyllableCounter(this.exports, sampleRate, channels);
  }

  /**
   * Create a listener trainer.
   * @param seed Seed for its random choices, an unsigned 64-bit number (a number or a bigint). Default 0.
   */
  createListenerTrainer(seed: bigint | number = 0): ListenerTrainer {
    return new ListenerTrainer(this.exports, seed64(seed));
  }

  /**
   * Create a designer of blind A/B trials.
   * @param seed Seed for its random choices, an unsigned 64-bit number (a number or a bigint). Default 0.
   */
  createBlindTrials(seed: bigint | number = 0): BlindTrials {
    return new BlindTrials(this.exports, seed64(seed));
  }
}

function seed64(seed: bigint | number): bigint {
  if (typeof seed === "number" && !Number.isInteger(seed)) throw new RangeError("seed must be a whole number");
  const value = BigInt(seed);
  if (value < 0n || value >= 1n << 64n) throw new RangeError("seed must be from 0 to 2^64 - 1");
  return value;
}

// Frees the memory of streams that are dropped without free() being called, where the host supports it.
const forgotten =
  typeof FinalizationRegistry === "undefined"
    ? undefined
    : new FinalizationRegistry<() => void>((release) => release());

/**
 * Speeds up speech. Write audio in, read the faster audio out.
 *
 * Audio is Float32Array in the range -1 to 1. A frame is one sample per channel. `write` and `read` take the
 * channels interleaved in one array; `writePlanar` and `readPlanar` take one array per channel, which is
 * what the Web Audio API uses.
 */
export class Stream {
  private stream: number;
  private scratch = 0;
  private scratchSamples = 0;

  /** @internal */
  constructor(
    private readonly exports: Exports,
    readonly sampleRate: number,
    readonly channels: number,
  ) {
    this.stream = exports.speechwarp_create(sampleRate, channels);
    if (this.stream === 0) throw new Error("speechwarp: out of memory");
    const stream = this.stream;
    forgotten?.register(this, () => exports.speechwarp_destroy(stream), this);
  }

  /**
   * Overall speed: 2 plays twice as fast. Clamped to MIN_SPEED..MAX_SPEED.
   *
   * Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
   * speed-up the speed varies from moment to moment and its average is steered to this value; expect the
   * result within a few percent.
   */
  get speed(): number {
    return this.exports.speechwarp_get_speed(this.open());
  }

  set speed(value: number) {
    if (!(value > 0)) throw new RangeError(`speed must be greater than zero, not ${value}`);
    this.exports.speechwarp_set_speed(this.open(), value);
  }

  /**
   * How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and pauses,
   * as a fast talker does. 0 compresses everything evenly. May be changed during playback.
   */
  get nonlinear(): number {
    return this.exports.speechwarp_get_nonlinear(this.open());
  }

  set nonlinear(value: number) {
    if (Number.isNaN(value)) throw new RangeError("nonlinear must be a number");
    this.exports.speechwarp_set_nonlinear(this.open(), value);
  }

  // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
  // docs/how-it-works.md.

  /**
   * Shorten every pause to at most this many seconds of input before speeding up, so that the speed is spent
   * on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. `position` counts the
   * frames left out.
   */
  get pauseCap(): number {
    return this.exports.speechwarp_get_pause_cap(this.open());
  }

  set pauseCap(value: number) {
    this.exports.speechwarp_set_pause_cap(this.open(), number("pauseCap", value));
  }

  /**
   * While the pause cap or rhythm is on, keep the overall speed (true, the default): time saved in pauses is
   * spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
   * When false, trimmed pauses make playback faster than `speed`.
   */
  get keepSpeed(): boolean {
    return this.exports.speechwarp_get_keep_speed(this.open()) !== 0;
  }

  set keepSpeed(value: boolean) {
    this.exports.speechwarp_set_keep_speed(this.open(), value ? 1 : 0);
  }

  /**
   * No 10 ms block of speech plays slower than this fraction of `speed`: at 8x, 0.5 keeps every block at 4x
   * or more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with
   * nonlinear speed-up above 1x.
   */
  get speedFloor(): number {
    return this.exports.speechwarp_get_speed_floor(this.open());
  }

  set speedFloor(value: number) {
    this.exports.speechwarp_set_speed_floor(this.open(), number("speedFloor", value));
  }

  /**
   * Seconds of silence put into the output `rhythmRate` times a second, at the quietest point nearby, which
   * can help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06.
   * Allowed: 0, or 0.005 to 0.2. `position` holds still during a gap.
   */
  get rhythmGap(): number {
    return this.exports.speechwarp_get_rhythm_gap(this.open());
  }

  set rhythmGap(value: number) {
    this.exports.speechwarp_set_rhythm_gap(this.open(), number("rhythmGap", value));
  }

  /** Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. */
  get rhythmRate(): number {
    return this.exports.speechwarp_get_rhythm_rate(this.open());
  }

  set rhythmRate(value: number) {
    if (!(value > 0)) throw new RangeError(`rhythmRate must be greater than zero, not ${value}`);
    this.exports.speechwarp_set_rhythm_rate(this.open(), value);
  }

  // Options that follow the speed. See docs/how-it-works.md.

  /**
   * Keep each pause about `seconds` long in the output: the pause cap in force is `seconds` times the current
   * speed, clamped to 0.03 to 0.4 s of input, and is applied whenever `speed` changes. Below `fromSpeed`
   * pauses are left alone. `seconds`: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to
   * 0.4. `fromSpeed`: 1 to 20. Sensible: 0.015 to 0.06 from 3x. Setting `pauseCap` turns the rule off.
   */
  setHeardPause(seconds: number, fromSpeed: number): void {
    this.exports.speechwarp_set_heard_pause(this.open(), number("seconds", seconds), number("fromSpeed", fromSpeed));
  }

  /** The `seconds` last given to `setHeardPause`; 0 while the rule is off. */
  get heardPause(): number {
    return this.exports.speechwarp_get_heard_pause(this.open());
  }

  /** The `fromSpeed` last given to `setHeardPause`. */
  get heardPauseFrom(): number {
    return this.exports.speechwarp_get_heard_pause_from(this.open());
  }

  /**
   * The speed floor in force is 0 below `fromSpeed`, rises linearly to `fraction` at `fullSpeed` and stays
   * there above it, so that a speed ramp never changes the sound in a jump. `fraction`: 0 turns the rule off
   * (and the floor with it); otherwise up to 1. Speeds 1 to 20; if `fullSpeed` is not above `fromSpeed` the
   * floor steps to `fraction` at `fromSpeed`. Sensible: 0.5 from 4x, full at 6x. Setting `speedFloor` turns
   * the rule off.
   */
  setFloorBlend(fraction: number, fromSpeed: number, fullSpeed: number): void {
    this.exports.speechwarp_set_floor_blend(
      this.open(), number("fraction", fraction), number("fromSpeed", fromSpeed), number("fullSpeed", fullSpeed),
    );
  }

  /** The `fraction` last given to `setFloorBlend`; 0 while the rule is off. */
  get floorBlend(): number {
    return this.exports.speechwarp_get_floor_blend(this.open());
  }

  /** The `fromSpeed` last given to `setFloorBlend`. */
  get floorBlendFrom(): number {
    return this.exports.speechwarp_get_floor_blend_from(this.open());
  }

  /** The `fullSpeed` last given to `setFloorBlend`. */
  get floorBlendFull(): number {
    return this.exports.speechwarp_get_floor_blend_full(this.open());
  }

  /**
   * Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
   * been written since creation or `reset`. Multiply by the speed for the rate heard. An estimate, typically
   * within about 10%.
   */
  get syllableRate(): number | null {
    const rate = this.exports.speechwarp_syllable_rate(this.open());
    return rate < 0 ? null : rate;
  }

  /** Frames of output ready to read. */
  get available(): number {
    return this.exports.speechwarp_available(this.open());
  }

  /**
   * The input frame, counted from creation or the last `reset`, that the next output frame to be read was
   * made from. This is how a player maps what is being heard back to a place in the source.
   *
   * It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
   * `flush` has been read it equals the number of frames written.
   */
  get position(): number {
    return Number(this.exports.speechwarp_position(this.open()));
  }

  /** Add interleaved input. The output does not depend on how the input is divided between calls. */
  write(samples: Float32Array): void {
    if (samples.length % this.channels !== 0) {
      throw new RangeError(`${samples.length} samples is not a whole number of ${this.channels}-channel frames`);
    }
    this.heap(samples.length).set(samples);
    this.send(samples.length / this.channels);
  }

  /** Add input given as one array per channel, all the same length. */
  writePlanar(channels: readonly Float32Array[]): void {
    const frames = this.planarFrames(channels);
    const heap = this.heap(frames * this.channels);
    for (let c = 0; c < this.channels; c++) {
      const channel = channels[c];
      for (let i = 0, at = c; i < frames; i++, at += this.channels) heap[at] = channel[i];
    }
    this.send(frames);
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
    if (target instanceof Float32Array) {
      const frames = this.receive(Math.floor(target.length / this.channels));
      target.set(this.heap(0).subarray(0, frames * this.channels));
      return frames;
    }
    const frames = this.receive(Math.min(target ?? Infinity, this.available));
    return this.heap(0).slice(0, frames * this.channels);
  }

  /**
   * Take output as one array per channel, all the same length. Returns the number of frames written to each;
   * the rest of each array is left alone.
   */
  readPlanar(channels: readonly Float32Array[]): number {
    const frames = this.receive(this.planarFrames(channels));
    const heap = this.heap(0);
    for (let c = 0; c < this.channels; c++) {
      const channel = channels[c];
      for (let i = 0, at = c; i < frames; i++, at += this.channels) channel[i] = heap[at];
    }
    return frames;
  }

  /**
   * Process everything written so far, at the end of the input. Read until empty afterwards. Writing more
   * starts a new stretch of audio, and `position` carries on counting.
   */
  flush(): void {
    if (!this.exports.speechwarp_flush(this.open())) throw new Error("speechwarp: out of memory");
  }

  /**
   * Discard all buffered input and output, keeping the speed and nonlinear settings, and start `position`
   * again from zero. Use after seeking.
   */
  reset(): void {
    this.exports.speechwarp_reset(this.open());
  }

  /** Release the stream's memory now. Using the stream afterwards throws. */
  free(): void {
    if (this.stream === 0) return;
    forgotten?.unregister(this);
    this.exports.speechwarp_destroy(this.stream);
    this.exports.free(this.scratch);
    this.stream = 0;
    this.scratch = 0;
    this.scratchSamples = 0;
  }

  private open(): number {
    if (this.stream === 0) throw new Error("speechwarp: the stream has been freed");
    return this.stream;
  }

  private planarFrames(channels: readonly Float32Array[]): number {
    if (channels.length !== this.channels) {
      throw new RangeError(`expected ${this.channels} channels, not ${channels.length}`);
    }
    const frames = channels[0].length;
    for (const channel of channels) {
      if (channel.length !== frames) throw new RangeError("the channels are not all the same length");
    }
    return frames;
  }

  /**
   * A view of the scratch space in the library's memory through which audio is passed, made large enough for
   * `samples`. Views go stale whenever that memory grows, so one is never kept across a call into the library.
   */
  private heap(samples: number): Float32Array {
    this.open();
    if (samples > this.scratchSamples) {
      this.exports.free(this.scratch);
      this.scratchSamples = Math.max(samples, 2 * this.scratchSamples, 4096);
      this.scratch = this.exports.malloc(this.scratchSamples * 4);
      if (this.scratch === 0) {
        this.scratchSamples = 0;
        throw new Error("speechwarp: out of memory");
      }
    }
    return new Float32Array(this.exports.memory.buffer, this.scratch, this.scratchSamples);
  }

  private send(frames: number): void {
    if (frames > 0 && !this.exports.speechwarp_write(this.stream, this.scratch, frames)) {
      throw new Error("speechwarp: out of memory");
    }
  }

  private receive(maxFrames: number): number {
    if (!(maxFrames > 0)) return 0;
    this.heap(maxFrames * this.channels);
    return this.exports.speechwarp_read(this.open(), this.scratch, maxFrames);
  }
}

/** Reusable space in the library's memory through which arrays are passed. */
class Scratch {
  private pointer = 0;
  private bytes = 0;

  constructor(private readonly exports: Exports) {}

  /** The address of space for `bytes`. Views of memory go stale when it grows, so make them after this. */
  get(bytes: number): number {
    if (bytes > this.bytes) {
      this.exports.free(this.pointer);
      this.bytes = Math.max(bytes, 2 * this.bytes, 16384);
      this.pointer = this.exports.malloc(this.bytes);
      if (this.pointer === 0) {
        this.bytes = 0;
        throw new Error("speechwarp: out of memory");
      }
    }
    return this.pointer;
  }

  release(): void {
    this.exports.free(this.pointer);
    this.pointer = 0;
    this.bytes = 0;
  }
}

/**
 * The syllable counter of a stream, on its own: for audio that does not go through a stream (a player using
 * some other speed-up, or measuring a file). A counter given the same input as a stream reports the same rate
 * as `Stream.syllableRate`. Get one from `Speechwarp.createSyllableCounter`.
 */
export class SyllableCounter {
  private counter: number;
  private readonly scratch: Scratch;

  /** @internal */
  constructor(
    private readonly exports: Exports,
    readonly sampleRate: number,
    readonly channels: number,
  ) {
    this.counter = exports.speechwarp_syllables_create(sampleRate, channels);
    if (this.counter === 0) throw new Error("speechwarp: out of memory");
    this.scratch = new Scratch(exports);
    const counter = this.counter;
    const scratch = this.scratch;
    forgotten?.register(this, () => { exports.speechwarp_syllables_destroy(counter); scratch.release(); }, this);
  }

  /** Add interleaved samples in the range -1 to 1, or 16-bit integers. */
  write(samples: Float32Array | Int16Array): void {
    const counter = this.open();
    if (samples.length % this.channels !== 0) {
      throw new RangeError(`${samples.length} samples is not a whole number of ${this.channels}-channel frames`);
    }
    const frames = samples.length / this.channels;
    if (frames === 0) return;
    const at = this.scratch.get(samples.byteLength);
    const ok =
      samples instanceof Int16Array
        ? (new Int16Array(this.exports.memory.buffer, at, samples.length).set(samples),
          this.exports.speechwarp_syllables_write_i16(counter, at, frames))
        : (new Float32Array(this.exports.memory.buffer, at, samples.length).set(samples),
          this.exports.speechwarp_syllables_write(counter, at, frames));
    if (!ok) throw new Error("speechwarp: the samples could not be counted");
  }

  /**
   * Syllables a second over the last `windowSeconds` written (or all of it, if less); null until
   * `minimumSeconds` have been written. The window is clamped to 1 to 120 s, the minimum to 0 to the window.
   */
  rate(windowSeconds = 60, minimumSeconds = 10): number | null {
    const rate = this.exports.speechwarp_syllables_rate(
      this.open(), number("windowSeconds", windowSeconds), number("minimumSeconds", minimumSeconds),
    );
    return rate < 0 ? null : rate;
  }

  /** Forget everything written. */
  reset(): void {
    this.exports.speechwarp_syllables_reset(this.open());
  }

  /** Release the counter's memory now. Using it afterwards throws. */
  free(): void {
    if (this.counter === 0) return;
    forgotten?.unregister(this);
    this.exports.speechwarp_syllables_destroy(this.counter);
    this.scratch.release();
    this.counter = 0;
  }

  private open(): number {
    if (this.counter === 0) throw new Error("speechwarp: the syllable counter has been freed");
    return this.counter;
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
 * are seconds on any clock, and only differences are used. NaN arguments are ignored. Get one from
 * `Speechwarp.createListenerTrainer`.
 */
export class ListenerTrainer {
  private trainer: number;

  /** @internal */
  constructor(private readonly exports: Exports, seed: bigint) {
    this.trainer = exports.speechwarp_trainer_create(seed);
    if (this.trainer === 0) throw new Error("speechwarp: out of memory");
    const trainer = this.trainer;
    forgotten?.register(this, () => exports.speechwarp_trainer_destroy(trainer), this);
  }

  /**
   * How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification
   * 1, retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
   */
  setWeight(kind: TrainerMeasure, weight: number): void {
    this.exports.speechwarp_trainer_set_weight(this.open(), kind, weight);
  }

  getWeight(kind: TrainerMeasure): number {
    return this.exports.speechwarp_trainer_get_weight(this.open(), kind);
  }

  /** Set a tunable number; see `TrainerParam` for the defaults. */
  setParam(param: TrainerParam, value: number): void {
    this.exports.speechwarp_trainer_set_param(this.open(), param, value);
  }

  getParam(param: TrainerParam): number {
    return this.exports.speechwarp_trainer_get_param(this.open(), param);
  }

  /**
   * Record a score: `kind` (not Retention), `score` 0..1, from `items` items (for a sentence repeated back,
   * the number of words scored; for verification, the number of questions; for a rating, 1), heard at `rate`
   * syllables a second, at `time`. During a threshold test it updates the estimate; during a session it is
   * an in-session check, and the tracking plan reacts to it. Returns false if an argument is invalid.
   */
  addMeasure(kind: TrainerMeasure, score: number, items: number, rate: number, time: number): boolean {
    return this.exports.speechwarp_trainer_add_measure(this.open(), kind, score, items, rate, time) !== 0;
  }

  /**
   * Start a threshold test: estimates the rate understood `TrainerParam.Target` (75%) of the time, by the psi
   * method. The prior is log-normal around `priorRate` or, if that is 0, around the last estimate, or failing
   * that around 10 syllables a second, within 3 to 60.
   */
  testBegin(priorRate: number, time: number): void {
    this.exports.speechwarp_trainer_test_begin(this.open(), priorRate, time);
  }

  /** The rate to present next. */
  testRate(): number {
    return this.exports.speechwarp_trainer_test_rate(this.open());
  }

  /**
   * True once the 95% interval is narrower than `TrainerParam.TestPrecision` (at least 8 presentations) or
   * `TrainerParam.TestMax` presentations have been scored; false otherwise or if no test is running.
   */
  get testDone(): boolean {
    return this.exports.speechwarp_trainer_test_done(this.open()) !== 0;
  }

  /** Finish the test; the estimate becomes the current threshold. Returns it (0 if no test was running). */
  testEnd(time: number): number {
    return this.exports.speechwarp_trainer_test_end(this.open(), time);
  }

  /** The current estimate (posterior median): of the running test, else of the last one finished; 0 if none. */
  get threshold(): number {
    return this.exports.speechwarp_trainer_threshold(this.open());
  }

  /** The low end of the 95% interval of the estimate. */
  get thresholdLow(): number {
    return this.exports.speechwarp_trainer_threshold_low(this.open());
  }

  /** The high end of the 95% interval of the estimate. */
  get thresholdHigh(): number {
    return this.exports.speechwarp_trainer_threshold_high(this.open());
  }

  /** Begin a session under `plan`. */
  sessionBegin(plan: TrainerPlan, time: number): void {
    this.exports.speechwarp_trainer_session_begin(this.open(), plan, time);
  }

  /** The rate to play at now, under the session's plan, from the threshold at `sessionBegin`. 0 if no session. */
  sessionRate(time: number): number {
    return this.exports.speechwarp_trainer_session_rate(this.open(), time);
  }

  /**
   * End the session after `listeningHours` of listening in it. It is recorded for comparing plans if a
   * threshold test ended after it began. Returns the session's number (0, 1, ...) for `addRetention`, or -1.
   */
  sessionEnd(listeningHours: number, time: number): number {
    return this.exports.speechwarp_trainer_session_end(this.open(), listeningHours, time);
  }

  /**
   * Retention for a recorded session: `score` 0..1 from `items` items, answered `delaySeconds` after it
   * ended. Returns false if an argument is invalid.
   */
  addRetention(session: number, score: number, items: number, delaySeconds: number, time: number): boolean {
    return this.exports.speechwarp_trainer_add_retention(this.open(), session, score, items, delaySeconds, time) !== 0;
  }

  /** The plan to run next: a Thompson draw (advances the random source). */
  nextPlan(): TrainerPlan {
    return this.exports.speechwarp_trainer_next_plan(this.open());
  }

  /** A plan's estimated threshold gain an hour now, as a fraction: 0.01 is 1% an hour. */
  planEffect(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_effect(this.open(), plan);
  }

  /** The standard deviation of `planEffect`. */
  planEffectSd(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_effect_sd(this.open(), plan);
  }

  /** A plan's retention; NaN without data. */
  planRetention(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_retention(this.open(), plan);
  }

  /** The standard deviation of `planRetention`; NaN without data. */
  planRetentionSd(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_retention_sd(this.open(), plan);
  }

  /** Sessions recorded under a plan. */
  planSessions(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_sessions(this.open(), plan);
  }

  /** The probability that a plan is the best by utility (does not advance the random source). */
  planBestProbability(plan: TrainerPlan): number {
    return this.exports.speechwarp_trainer_plan_best_probability(this.open(), plan);
  }

  /** H, the hours of listening by which gains have halved (1000 standing for "not slowing"). */
  get trend(): number {
    return this.exports.speechwarp_trainer_trend(this.open());
  }

  /** The uncertainty of `trend` as a standard deviation of ln H. */
  get trendSd(): number {
    return this.exports.speechwarp_trainer_trend_sd(this.open());
  }

  /** Release the trainer's memory now. Using it afterwards throws. */
  free(): void {
    if (this.trainer === 0) return;
    forgotten?.unregister(this);
    this.exports.speechwarp_trainer_destroy(this.trainer);
    this.trainer = 0;
  }

  private open(): number {
    if (this.trainer === 0) throw new Error("speechwarp: the trainer has been freed");
    return this.trainer;
  }
}

/**
 * Designing the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of
 * its values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...).
 * The caller names the settings; here they are numbers. Deterministic for a given seed. Get one from
 * `Speechwarp.createBlindTrials`.
 */
export class BlindTrials {
  private trials: number;

  /** @internal */
  constructor(private readonly exports: Exports, seed: bigint) {
    this.trials = exports.speechwarp_trials_create(seed);
    if (this.trials === 0) throw new Error("speechwarp: out of memory");
    const trials = this.trials;
    forgotten?.register(this, () => exports.speechwarp_trials_destroy(trials), this);
  }

  /** Add a setting; returns its number (0, 1, ...), or -1. */
  addSetting(): number {
    return this.exports.speechwarp_trials_add_setting(this.open());
  }

  /** Add a value to compare; returns its number within the setting, or -1 (duplicate, bad setting). */
  addValue(setting: number, value: number): number {
    return this.exports.speechwarp_trials_add_value(this.open(), setting, value);
  }

  /** Leave a setting out of `next` while false (say, when its method is not available). Default true. */
  setAvailable(setting: number, available: boolean): void {
    this.exports.speechwarp_trials_set_available(this.open(), setting, available ? 1 : 0);
  }

  /**
   * Record a trial at `speed`: the two values in the order heard, each one's score 0..1, and `preferred`: -1
   * the first, 1 the second, 0 neither. Values must be ones added. Returns false if an argument is invalid.
   */
  add(
    setting: number, speed: number, firstValue: number, secondValue: number,
    firstScore: number, secondScore: number, preferred: number,
  ): boolean {
    return this.exports.speechwarp_trials_add(
      this.open(), setting, speed, firstValue, secondValue, firstScore, secondScore, preferred,
    ) !== 0;
  }

  /**
   * Choose the next trial at `speed`: the available setting with the fewest trials in its band (ties at
   * random), its pair of values compared least (ties at random), in random order. Null if no setting has two
   * values.
   */
  next(speed: number): { setting: number; first: number; second: number } | null {
    const trials = this.open();
    const setting = this.exports.speechwarp_trials_next(trials, speed);
    if (setting < 0) return null;
    return {
      setting,
      first: this.exports.speechwarp_trials_next_first(trials),
      second: this.exports.speechwarp_trials_next_second(trials),
    };
  }

  /** Comparisons won by one value of a setting in the band of `speed`. */
  won(setting: number, speed: number, value: number): number {
    return this.exports.speechwarp_trials_won(this.open(), setting, speed, value);
  }

  /** Comparisons lost by one value of a setting in the band of `speed`. */
  lost(setting: number, speed: number, value: number): number {
    return this.exports.speechwarp_trials_lost(this.open(), setting, speed, value);
  }

  /** Comparisons tied by one value of a setting in the band of `speed`. */
  tied(setting: number, speed: number, value: number): number {
    return this.exports.speechwarp_trials_tied(this.open(), setting, speed, value);
  }

  /** Trials one value of a setting was heard in, in the band of `speed`. */
  heard(setting: number, speed: number, value: number): number {
    return this.exports.speechwarp_trials_heard(this.open(), setting, speed, value);
  }

  /** The mean score of one value in the band of `speed`; null if never heard. */
  meanScore(setting: number, speed: number, value: number): number | null {
    const score = this.exports.speechwarp_trials_mean_score(this.open(), setting, speed, value);
    return Number.isNaN(score) ? null : score;
  }

  /**
   * The value with a reliable win in that band, or null. A value wins when it has been heard in at least 5
   * trials, has met every other value in at least 3, and against each the Bayes factor for "preferred" over
   * "no preference" is at least 1 / (1 - confidence): 20 at the default 0.95. However often this is asked,
   * the chance of ever naming a winner between two values that are really alike is at most 1 - confidence
   * each way.
   */
  winner(setting: number, speed: number): number | null {
    const value = this.exports.speechwarp_trials_winner(this.open(), setting, speed);
    return value < 0 ? null : value;
  }

  /** The confidence `winner` demands, 0 to 1; default 0.95. */
  setConfidence(confidence: number): void {
    this.exports.speechwarp_trials_set_confidence(this.open(), confidence);
  }

  /** Release the trials' memory now. Using them afterwards throws. */
  free(): void {
    if (this.trials === 0) return;
    forgotten?.unregister(this);
    this.exports.speechwarp_trials_destroy(this.trials);
    this.trials = 0;
  }

  private open(): number {
    if (this.trials === 0) throw new Error("speechwarp: the trials have been freed");
    return this.trials;
  }
}
