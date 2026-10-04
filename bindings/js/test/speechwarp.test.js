import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { MAX_SPEED, MIN_SPEED, load, loadSync } from "../dist/index.js";

const RATE = 22050;
const speechwarp = await load();

/** Something for the library to chew on: a gliding tone in bursts, with gaps. */
function signal(seconds, channels = 1) {
  const frames = Math.floor(RATE * seconds);
  const samples = new Float32Array(frames * channels);
  let phase = 0;
  for (let i = 0; i < frames; i++) {
    const t = i / RATE;
    phase += (2 * Math.PI * (120 + 60 * Math.sin(t * 3))) / RATE;
    const envelope = t % 0.4 < 0.3 ? Math.sin((Math.PI * (t % 0.4)) / 0.3) : 0;
    const value = 0.4 * envelope * (Math.sin(phase) + 0.5 * Math.sin(2 * phase) + 0.3 * Math.sin(3 * phase));
    for (let c = 0; c < channels; c++) samples[i * channels + c] = value;
  }
  return samples;
}

function speedUp(input, speed, nonlinear = 1, channels = 1) {
  const stream = speechwarp.createStream(RATE, channels);
  stream.speed = speed;
  stream.nonlinear = nonlinear;
  stream.write(input);
  stream.flush();
  const out = stream.read();
  stream.free();
  return out;
}

test("the version matches the header and the package", () => {
  const header = readFileSync(new URL("../../../include/speechwarp.h", import.meta.url), "utf8");
  const pack = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8"));
  assert.equal(speechwarp.version, /#define SPEECHWARP_VERSION "(.*)"/.exec(header)[1]);
  assert.equal(speechwarp.version, pack.version);
});

test("load is shared and loadSync works", async () => {
  assert.equal(await load(), speechwarp);
  assert.equal(loadSync().version, speechwarp.version);
});

test("defaults and arguments", () => {
  const stream = speechwarp.createStream(RATE, 2);
  assert.deepEqual([stream.sampleRate, stream.channels, stream.speed, stream.nonlinear], [RATE, 2, 1, 1]);
  assert.deepEqual([stream.available, stream.position, stream.read().length], [0, 0, 0]);

  assert.throws(() => speechwarp.createStream(100), RangeError);
  assert.throws(() => speechwarp.createStream(RATE, 0), RangeError);
  assert.throws(() => (stream.speed = 0), RangeError);
  assert.throws(() => (stream.speed = NaN), RangeError);
  assert.throws(() => (stream.nonlinear = NaN), RangeError);
  assert.throws(() => stream.write(new Float32Array(3)), RangeError);
  assert.throws(() => stream.writePlanar([new Float32Array(4)]), RangeError);
  assert.throws(() => stream.writePlanar([new Float32Array(4), new Float32Array(5)]), RangeError);

  stream.speed = 1000;
  assert.equal(stream.speed, MAX_SPEED);
  stream.speed = 1e-6;
  assert.ok(Math.abs(stream.speed - MIN_SPEED) < 1e-6);
  stream.nonlinear = -3;
  assert.equal(stream.nonlinear, 0);

  stream.free();
  stream.free();
  assert.throws(() => stream.available, /freed/);
  assert.throws(() => stream.write(new Float32Array(4)), /freed/);
});

test("output is shorter by the speed", () => {
  const input = signal(20);
  for (const [speed, nonlinear] of [[1, 1], [3, 1], [3, 0], [8, 1]]) {
    const output = speedUp(input, speed, nonlinear);
    assert.ok(Math.abs(input.length / output.length / speed - 1) < 0.1, `speed ${speed}`);
  }
});

test("planar and interleaved agree", () => {
  const mono = signal(5);
  const quiet = mono.map((x) => x / 2);
  const interleaved = new Float32Array(mono.length * 2);
  for (let i = 0; i < mono.length; i++) {
    interleaved[2 * i] = mono[i];
    interleaved[2 * i + 1] = quiet[i];
  }
  const expected = speedUp(interleaved, 2, 1, 2);

  const stream = speechwarp.createStream(RATE, 2);
  stream.speed = 2;
  stream.writePlanar([mono, quiet]);
  stream.flush();
  const left = new Float32Array(expected.length / 2 + 10).fill(9);
  const right = new Float32Array(expected.length / 2 + 10).fill(9);
  assert.equal(stream.readPlanar([left, right]), expected.length / 2);
  for (let i = 0; i < expected.length / 2; i++) {
    assert.equal(left[i], expected[2 * i]);
    assert.equal(right[i], expected[2 * i + 1]);
  }
  assert.equal(left[expected.length / 2], 9); // the rest is left alone
  stream.free();
});

test("streaming matches one shot, and position follows", () => {
  const input = signal(20);
  const stream = speechwarp.createStream(RATE);
  stream.speed = 4;
  const pieces = [];
  const buffer = new Float32Array(256);
  let last = 0;
  for (let start = 0; start < input.length; start += 1000) {
    stream.write(input.subarray(start, start + 1000));
    while (stream.available >= 256) {
      assert.ok(stream.position >= last && stream.position <= start + 1000);
      last = stream.position;
      assert.equal(stream.read(buffer), 256);
      pieces.push(buffer.slice());
    }
  }
  stream.flush();
  pieces.push(stream.read());
  assert.equal(stream.position, input.length);

  const expected = speedUp(input, 4);
  assert.equal(pieces.reduce((n, p) => n + p.length, 0), expected.length);
  let at = 0;
  for (const piece of pieces) {
    assert.deepEqual(piece, expected.subarray(at, at + piece.length));
    at += piece.length;
  }
  stream.free();
});

test("reset discards everything", () => {
  const input = signal(5);
  const stream = speechwarp.createStream(RATE);
  stream.speed = 3;
  stream.write(input);
  assert.ok(stream.available > 0);
  stream.reset();
  assert.deepEqual([stream.available, stream.position, stream.speed], [0, 0, 3]);
  stream.write(input);
  stream.flush();
  assert.deepEqual(stream.read(), speedUp(input, 3));
  stream.free();
});

test("a long recording makes the memory grow without harm", () => {
  const input = signal(600);
  const output = speedUp(input, 2);
  assert.ok(Math.abs(input.length / output.length / 2 - 1) < 0.1);
});

test("the options for high speeds start off and are clamped", () => {
  const stream = speechwarp.createStream(RATE);
  assert.deepEqual(
    [stream.pauseCap, stream.keepSpeed, stream.speedFloor, stream.rhythmGap, stream.rhythmRate, stream.syllableRate],
    [0, true, 0, 0, 5, null],
  );
  stream.pauseCap = 5;
  stream.speedFloor = 0.5;
  stream.rhythmGap = 0.04;
  stream.rhythmRate = 100;
  stream.keepSpeed = false;
  assert.deepEqual([stream.pauseCap, stream.speedFloor, stream.rhythmRate, stream.keepSpeed], [1, 0.5, 16, false]);
  assert.ok(Math.abs(stream.rhythmGap - 0.04) < 1e-6);
  assert.throws(() => (stream.pauseCap = NaN), RangeError);
  assert.throws(() => (stream.rhythmRate = 0), RangeError);
  stream.free();
});

test("the pause cap shortens pauses and position still reaches the end", () => {
  const input = signal(12);
  const stream = speechwarp.createStream(RATE);
  stream.nonlinear = 0;
  stream.pauseCap = 0.03;
  stream.keepSpeed = false;
  stream.write(input);
  stream.flush();
  assert.ok(stream.read().length < input.length * 0.9);
  assert.equal(stream.position, input.length);
  assert.ok(stream.syllableRate > 0);
  stream.free();
});
