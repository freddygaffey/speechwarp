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
  speechwarp_write(stream: number, samples: number, frames: number): number;
  speechwarp_read(stream: number, samples: number, maxFrames: number): number;
  speechwarp_available(stream: number): number;
  speechwarp_flush(stream: number): number;
  speechwarp_reset(stream: number): void;
  speechwarp_position(stream: number): bigint;
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
  /** The version of the C library, such as "0.1.0". */
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
