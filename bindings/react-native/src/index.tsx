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

/** The version of the native library, such as "0.2.0". */
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
