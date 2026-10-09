import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { MAX_SPEED, MIN_SPEED, TrainerMeasure, TrainerParam, TrainerPlan, load, loadSync } from "../dist/index.js";

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

test("heard pause and floor blend are read back, and drive the pause cap and floor", () => {
  const stream = speechwarp.createStream(RATE);
  assert.deepEqual([stream.heardPause, stream.floorBlend], [0, 0]);
  stream.setHeardPause(0.03, 3);
  assert.ok(Math.abs(stream.heardPause - 0.03) < 1e-6);
  assert.equal(stream.heardPauseFrom, 3);
  stream.speed = 5;
  assert.ok(Math.abs(stream.pauseCap - 0.15) < 1e-6);
  stream.speed = 2; // below the from speed: pauses are left alone
  assert.equal(stream.pauseCap, 0);

  stream.setFloorBlend(0.5, 4, 6);
  assert.deepEqual([stream.floorBlend, stream.floorBlendFrom, stream.floorBlendFull], [0.5, 4, 6]);
  stream.speed = 5;
  assert.ok(Math.abs(stream.speedFloor - 0.25) < 1e-6);
  stream.speed = 8;
  assert.ok(Math.abs(stream.speedFloor - 0.5) < 1e-6);
  assert.throws(() => stream.setHeardPause(NaN, 3), RangeError);
  stream.free();
});

test("a syllable counter agrees with a stream", () => {
  const input = signal(15);
  const counter = speechwarp.createSyllableCounter(RATE, 1);
  const stream = speechwarp.createStream(RATE, 1);
  const half = RATE * 5;
  counter.write(input.subarray(0, half));
  assert.equal(counter.rate(), null);
  counter.write(input.subarray(half));
  stream.write(input);
  assert.ok(counter.rate() > 0);
  assert.equal(counter.rate(60, 10), stream.syllableRate);

  const ints = Int16Array.from(input, (x) => Math.round(x * 32767));
  const other = speechwarp.createSyllableCounter(RATE);
  other.write(ints);
  assert.ok(Math.abs(other.rate() - counter.rate()) < 0.5);

  counter.reset();
  assert.equal(counter.rate(), null);
  const stereo = speechwarp.createSyllableCounter(RATE, 2);
  assert.throws(() => stereo.write(new Float32Array(3)), RangeError);
  stereo.free();
  counter.free();
  counter.free();
  assert.throws(() => counter.rate(), /freed/);
  assert.throws(() => speechwarp.createSyllableCounter(100), RangeError);
  stream.free();
  other.free();
});

test("the listener trainer finds a threshold deterministically", () => {
  const run = () => {
    const trainer = speechwarp.createListenerTrainer(5n);
    trainer.testBegin(10, 0);
    for (let i = 0; i < 12; i++) {
      const rate = trainer.testRate();
      assert.ok(trainer.addMeasure(TrainerMeasure.Verification, rate < 11 ? 0.95 : 0.55, 8, rate, i));
    }
    return trainer;
  };
  const trainer = run();
  const threshold = trainer.testEnd(100);
  assert.ok(threshold > 0);
  assert.equal(trainer.threshold, threshold);
  assert.ok(trainer.thresholdLow < threshold && threshold < trainer.thresholdHigh);
  assert.equal(typeof trainer.testDone, "boolean");

  trainer.sessionBegin(TrainerPlan.Steady, 200);
  assert.ok(Math.abs(trainer.sessionRate(300) - threshold * 1.1) < 1e-6);
  assert.equal(trainer.sessionEnd(1, 3800), 0); // the first session recorded

  assert.equal(trainer.planEffect(TrainerPlan.Ramp), 0);
  assert.ok(Number.isNaN(trainer.planRetention(TrainerPlan.Ramp)));
  assert.equal(trainer.planSessions(TrainerPlan.Ramp), 0);

  trainer.setParam(TrainerParam.Target, 0.8);
  assert.equal(trainer.getParam(TrainerParam.Target), 0.8);
  trainer.setWeight(TrainerMeasure.Rating, 0.1);
  assert.ok(Math.abs(trainer.getWeight(TrainerMeasure.Rating) - 0.1) < 1e-12);
  assert.equal(trainer.addMeasure(TrainerMeasure.Retention, 0.5, 1, 10, 0), false);

  const a = speechwarp.createListenerTrainer(9);
  const b = speechwarp.createListenerTrainer(9n);
  const plansOf = (t) => Array.from({ length: 8 }, () => t.nextPlan());
  const plans = plansOf(a);
  assert.deepEqual(plansOf(b), plans);
  assert.ok(plans.every((p) => p >= 0 && p < 4));
  assert.throws(() => speechwarp.createListenerTrainer(-1), RangeError);
  for (const t of [trainer, a, b]) t.free();
  assert.throws(() => trainer.threshold, /freed/);
});

test("the trainer enums carry the C values, and every parameter, retention and summary reads back", () => {
  assert.deepEqual(
    [TrainerMeasure.Intelligibility, TrainerMeasure.Verification, TrainerMeasure.Retention, TrainerMeasure.Rating],
    [0, 1, 2, 3],
  );
  assert.deepEqual([TrainerPlan.Steady, TrainerPlan.Ramp, TrainerPlan.Interval, TrainerPlan.Tracking], [0, 1, 2, 3]);
  const names = ["Target", "Margin", "RampStart", "RampStep", "RampMinutes", "IntervalSpread", "IntervalMinutes",
    "TrackingGain", "RetentionCost", "TestMax", "TestPrecision"];
  names.forEach((name, i) => assert.equal(TrainerParam[name], i));

  const trainer = speechwarp.createListenerTrainer(3);
  assert.equal(trainer.getParam(TrainerParam.Target), 0.75);
  assert.equal(trainer.getParam(TrainerParam.TestMax), 40);
  for (const name of names) {
    const before = trainer.getParam(TrainerParam[name]);
    assert.ok(Number.isFinite(before), name);
  }
  trainer.setParam(TrainerParam.Margin, 0.2);
  assert.ok(Math.abs(trainer.getParam(TrainerParam.Margin) - 0.2) < 1e-12);

  trainer.testBegin(10, 0);
  for (let i = 0; i < 12; i++) {
    const rate = trainer.testRate();
    trainer.addMeasure(TrainerMeasure.Verification, rate < 11 ? 0.95 : 0.55, 8, rate, i);
  }
  trainer.testEnd(100);
  trainer.sessionBegin(TrainerPlan.Ramp, 200);
  trainer.testBegin(10, 300);
  for (let i = 0; i < 12; i++) {
    const rate = trainer.testRate();
    trainer.addMeasure(TrainerMeasure.Verification, rate < 11.5 ? 0.95 : 0.55, 8, rate, 300 + i);
  }
  trainer.testEnd(400); // a test after the session began, so the session is recorded
  const session = trainer.sessionEnd(1, 3800);
  assert.equal(session, 0);
  assert.equal(trainer.addRetention(session, 0.8, 8, 86400, 90000), true);
  assert.equal(trainer.addRetention(99, 0.8, 8, 86400, 90000), false);
  for (const plan of [TrainerPlan.Steady, TrainerPlan.Ramp, TrainerPlan.Interval, TrainerPlan.Tracking]) {
    assert.equal(typeof trainer.planEffectSd(plan), "number");
    assert.equal(typeof trainer.planRetentionSd(plan), "number");
    const p = trainer.planBestProbability(plan);
    assert.ok(p >= 0 && p <= 1);
  }
  assert.equal(trainer.planSessions(TrainerPlan.Ramp), 1);
  assert.ok(trainer.trend > 0);
  assert.ok(trainer.trendSd >= 0);
  trainer.free();
});

test("blind trials choose pairs and add up results", () => {
  const trials = speechwarp.createBlindTrials(1);
  assert.equal(trials.next(5), null);
  const setting = trials.addSetting();
  assert.equal(setting, 0);
  assert.equal(trials.addValue(setting, 0), 0);
  assert.equal(trials.addValue(setting, 0.5), 1);
  assert.equal(trials.addValue(setting, 0.5), -1);
  const next = trials.next(5);
  assert.equal(next.setting, setting);
  assert.deepEqual([next.first, next.second].sort(), [0, 0.5]);

  assert.equal(trials.meanScore(setting, 5, 0), null);
  assert.equal(trials.winner(setting, 5), null);
  assert.ok(trials.add(setting, 5, 0, 0.5, 0.6, 0.8, 1));
  assert.ok(trials.add(setting, 5.5, 0.5, 0, 0.9, 0.7, -1));
  assert.ok(trials.add(setting, 5, 0, 0.5, 0.5, 0.5, 0));
  assert.equal(trials.add(setting, 5, 0, 9, 0.5, 0.5, 0), false);
  assert.deepEqual([0, 1].map((v) => trials.won(setting, 5, v)), [0, 2]);
  assert.deepEqual([0, 1].map((v) => trials.lost(setting, 5, v)), [2, 0]);
  assert.deepEqual([0, 1].map((v) => trials.tied(setting, 5, v)), [1, 1]);
  assert.deepEqual([0, 1].map((v) => trials.heard(setting, 5, v)), [3, 3]);
  assert.ok(Math.abs(trials.meanScore(setting, 5, 0) - (0.6 + 0.7 + 0.5) / 3) < 1e-9);
  assert.equal(trials.winner(setting, 5), null);
  trials.setAvailable(setting, false);
  assert.equal(trials.next(5), null);
  trials.setConfidence(0.9);
  trials.free();
  assert.throws(() => trials.addSetting(), /freed/);
});

test("scoreWords aligns the words heard with the sentence", () => {
  assert.deepEqual(speechwarp.scoreWords("The cat sat on the mat.", '"the CAT, sat on the mat!"'), {
    share: 1, right: 6, missed: 0, wrong: 0, extra: 0,
  });
  assert.deepEqual(speechwarp.scoreWords("one two three four", "one too tree four five"), {
    share: 0.5, right: 2, missed: 0, wrong: 2, extra: 1,
  });
  assert.deepEqual(speechwarp.scoreWords("a b", "b a"), { share: 0.5, right: 1, missed: 1, wrong: 0, extra: 1 });
  assert.equal(speechwarp.scoreWords("Don't stop", "don\u2019t stop").share, 1);
  assert.equal(speechwarp.scoreWords("Caf\u00e9 au lait", "CAF\u00c9 au lait").share, 1);
  assert.equal(speechwarp.scoreWords("caf\u00e9", "cafe\u0301").share, 1); // normalised to NFC first
  assert.deepEqual(speechwarp.scoreWords("hi \u{1F600}x", "HI \u{1F600}x"), {
    share: 1, right: 2, missed: 0, wrong: 0, extra: 0,
  });
  assert.deepEqual(speechwarp.scoreWords("", ""), { share: 1, right: 0, missed: 0, wrong: 0, extra: 0 });
  assert.deepEqual(speechwarp.scoreWords("", "hello"), { share: 0, right: 0, missed: 0, wrong: 0, extra: 1 });
  assert.deepEqual(speechwarp.scoreWords("hello world", ""), { share: 0, right: 0, missed: 2, wrong: 0, extra: 0 });
});
