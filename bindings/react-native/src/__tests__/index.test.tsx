// Checks the JavaScript half against a stand-in for the native module: argument checking, the frames and
// samples arithmetic, and what is passed across. The native half is exercised by the example app.
import { beforeEach, describe, expect, it, jest } from '@jest/globals';

const mockNative = {
  version: jest.fn(() => '0.3.5'),
  createStream: jest.fn((_rate: number, _channels: number) => 7),
  destroyStream: jest.fn(),
  setSpeed: jest.fn(),
  getSpeed: jest.fn(() => 3),
  setNonlinear: jest.fn(),
  getNonlinear: jest.fn(() => 1),
  setPauseCap: jest.fn(),
  getPauseCap: jest.fn(() => 0.06),
  setKeepSpeed: jest.fn(),
  getKeepSpeed: jest.fn(() => true),
  setSpeedFloor: jest.fn(),
  getSpeedFloor: jest.fn(() => 0.5),
  setRhythmGap: jest.fn(),
  getRhythmGap: jest.fn(() => 0.04),
  setRhythmRate: jest.fn(),
  getRhythmRate: jest.fn(() => 6),
  syllableRate: jest.fn(() => -1),
  write: jest.fn((_h: number, _b: object, _o: number, _f: number) => true),
  read: jest.fn((_h: number, _b: object, _o: number, maxFrames: number) => Math.min(maxFrames, 10)),
  available: jest.fn(() => 10),
  flush: jest.fn(() => true),
  reset: jest.fn(),
  position: jest.fn(() => 1234),
  setHeardPause: jest.fn(),
  getHeardPause: jest.fn(() => 0.03),
  getHeardPauseFrom: jest.fn(() => 3),
  setFloorBlend: jest.fn(),
  getFloorBlend: jest.fn(() => 0.5),
  getFloorBlendFrom: jest.fn(() => 4),
  getFloorBlendFull: jest.fn(() => 6),
  syllablesCreate: jest.fn((_rate: number, _channels: number) => 11),
  syllablesDestroy: jest.fn(),
  syllablesWrite: jest.fn((_h: number, _b: object, _o: number, _f: number) => true),
  syllablesWriteI16: jest.fn((_h: number, _b: object, _o: number, _f: number) => true),
  syllablesRate: jest.fn((_h: number, _w: number, _m: number) => -1),
  syllablesReset: jest.fn(),
  trainerCreate: jest.fn((_seed: number) => 21),
  trainerDestroy: jest.fn(),
  trainerSetWeight: jest.fn(),
  trainerGetWeight: jest.fn(() => 0.5),
  trainerSetParam: jest.fn(),
  trainerGetParam: jest.fn(() => 0.75),
  trainerAddMeasure: jest.fn(() => true),
  trainerTestBegin: jest.fn(),
  trainerTestRate: jest.fn(() => 10),
  trainerTestDone: jest.fn(() => false),
  trainerTestEnd: jest.fn(() => 12),
  trainerThreshold: jest.fn(() => 12),
  trainerThresholdLow: jest.fn(() => 10),
  trainerThresholdHigh: jest.fn(() => 14),
  trainerSessionBegin: jest.fn(),
  trainerSessionRate: jest.fn(() => 13.2),
  trainerSessionEnd: jest.fn(() => 0),
  trainerAddRetention: jest.fn(() => true),
  trainerNextPlan: jest.fn(() => 2),
  trainerPlanEffect: jest.fn(() => 0),
  trainerPlanEffectSd: jest.fn(() => 0.1),
  trainerPlanRetention: jest.fn(() => NaN),
  trainerPlanRetentionSd: jest.fn(() => NaN),
  trainerPlanSessions: jest.fn(() => 0),
  trainerPlanBestProbability: jest.fn(() => 0.25),
  trainerTrend: jest.fn(() => 1000),
  trainerTrendSd: jest.fn(() => 1),
  trialsCreate: jest.fn((_seed: number) => 31),
  trialsDestroy: jest.fn(),
  trialsAddSetting: jest.fn(() => 0),
  trialsAddValue: jest.fn(() => 0),
  trialsSetAvailable: jest.fn(),
  trialsAdd: jest.fn(() => true),
  trialsNext: jest.fn(() => -1),
  trialsNextFirst: jest.fn(() => 0),
  trialsNextSecond: jest.fn(() => 0.5),
  trialsWon: jest.fn(() => 2),
  trialsLost: jest.fn(() => 1),
  trialsTied: jest.fn(() => 0),
  trialsHeard: jest.fn(() => 3),
  trialsMeanScore: jest.fn(() => NaN),
  trialsWinner: jest.fn(() => -1),
  trialsSetConfidence: jest.fn(),
};
jest.mock('../NativeSpeechwarp', () => ({ __esModule: true, default: mockNative }));

const {
  Stream,
  SyllableCounter,
  ListenerTrainer,
  BlindTrials,
  TrainerMeasure,
  TrainerPlan,
  TrainerParam,
  version,
  MAX_SPEED,
  MIN_SPEED,
} = require('../index') as typeof import('../index');

beforeEach(() => {
  jest.clearAllMocks();
});

describe('Stream', () => {
  it('reports the version and limits', () => {
    expect(version()).toBe('0.3.5');
    expect([MIN_SPEED, MAX_SPEED]).toEqual([0.05, 20]);
  });

  it('checks the format before creating anything', () => {
    expect(() => new Stream(100)).toThrow(RangeError);
    expect(() => new Stream(44100, 0)).toThrow(RangeError);
    expect(() => new Stream(44100.5)).toThrow(RangeError);
    expect(mockNative.createStream).not.toHaveBeenCalled();
    mockNative.createStream.mockReturnValueOnce(0);
    expect(() => new Stream(44100)).toThrow(/memory/);
  });

  it('passes settings through and rejects bad ones', () => {
    const stream = new Stream(44100, 2);
    stream.speed = 3;
    stream.nonlinear = 0;
    expect(mockNative.setSpeed).toHaveBeenCalledWith(7, 3);
    expect(mockNative.setNonlinear).toHaveBeenCalledWith(7, 0);
    expect(() => (stream.speed = 0)).toThrow(RangeError);
    expect(() => (stream.speed = NaN)).toThrow(RangeError);
    expect(() => (stream.nonlinear = NaN)).toThrow(RangeError);
    expect([stream.speed, stream.nonlinear, stream.available, stream.position]).toEqual([3, 1, 10, 1234]);
  });

  it('writes whole frames, with the offset of a view', () => {
    const stream = new Stream(44100, 2);
    const backing = new Float32Array(100);
    const view = backing.subarray(10, 30);
    stream.write(view);
    expect(mockNative.write).toHaveBeenCalledWith(7, backing.buffer, 40, 10);
    expect(() => stream.write(new Float32Array(3))).toThrow(RangeError);
    stream.write(new Float32Array(0));
    expect(mockNative.write).toHaveBeenCalledTimes(1);
  });

  it('reads into an array or makes one', () => {
    const stream = new Stream(44100, 2);
    const into = new Float32Array(9); // room for 4 frames, not 4.5
    expect(stream.read(into)).toBe(4);
    expect(mockNative.read).toHaveBeenLastCalledWith(7, into.buffer, 0, 4);

    expect(stream.read().length).toBe(20); // everything: 10 frames
    expect(stream.read(3).length).toBe(6);

    mockNative.read.mockReturnValueOnce(2); // fewer than were said to be ready
    expect(stream.read(5).length).toBe(4);
  });

  it('throws once freed, and frees only once', () => {
    const stream = new Stream(44100);
    stream.flush();
    stream.reset();
    stream.free();
    stream.free();
    expect(mockNative.destroyStream).toHaveBeenCalledTimes(1);
    expect(() => stream.available).toThrow(/freed/);
    expect(() => stream.write(new Float32Array(2))).toThrow(/freed/);
  });

  it('passes the options for high speeds across and checks them', () => {
    const stream = new Stream(44100);
    stream.pauseCap = 0.06;
    stream.keepSpeed = false;
    stream.speedFloor = 0.5;
    stream.rhythmGap = 0.04;
    stream.rhythmRate = 6;
    expect(mockNative.setPauseCap).toHaveBeenCalledWith(7, 0.06);
    expect(mockNative.setKeepSpeed).toHaveBeenCalledWith(7, false);
    expect(mockNative.setSpeedFloor).toHaveBeenCalledWith(7, 0.5);
    expect(mockNative.setRhythmGap).toHaveBeenCalledWith(7, 0.04);
    expect(mockNative.setRhythmRate).toHaveBeenCalledWith(7, 6);
    expect([stream.pauseCap, stream.keepSpeed, stream.speedFloor, stream.rhythmGap, stream.rhythmRate]).toEqual([
      0.06, true, 0.5, 0.04, 6,
    ]);
    expect(stream.syllableRate).toBeNull();
    mockNative.syllableRate.mockReturnValueOnce(4.5);
    expect(stream.syllableRate).toBe(4.5);
    expect(() => (stream.pauseCap = NaN)).toThrow(RangeError);
    expect(() => (stream.rhythmRate = 0)).toThrow(RangeError);
  });
});

describe('options that follow the speed', () => {
  it('passes the heard pause and floor blend across and reads them back', () => {
    const stream = new Stream(44100);
    stream.setHeardPause(0.03, 3);
    stream.setFloorBlend(0.5, 4, 6);
    expect(mockNative.setHeardPause).toHaveBeenCalledWith(7, 0.03, 3);
    expect(mockNative.setFloorBlend).toHaveBeenCalledWith(7, 0.5, 4, 6);
    expect([stream.heardPause, stream.heardPauseFrom]).toEqual([0.03, 3]);
    expect([stream.floorBlend, stream.floorBlendFrom, stream.floorBlendFull]).toEqual([0.5, 4, 6]);
    expect(() => stream.setHeardPause(NaN, 3)).toThrow(RangeError);
    expect(() => stream.setFloorBlend(0.5, 4, NaN)).toThrow(RangeError);
  });
});

describe('SyllableCounter', () => {
  it('checks the format and passes samples with their offset', () => {
    expect(() => new SyllableCounter(100)).toThrow(RangeError);
    expect(() => new SyllableCounter(44100, 33)).toThrow(RangeError);
    const counter = new SyllableCounter(44100, 2);
    const backing = new Float32Array(100);
    counter.write(backing.subarray(10, 30));
    expect(mockNative.syllablesWrite).toHaveBeenCalledWith(11, backing.buffer, 40, 10);
    const ints = new Int16Array(40);
    counter.write(ints.subarray(8, 28));
    expect(mockNative.syllablesWriteI16).toHaveBeenCalledWith(11, ints.buffer, 16, 10);
    expect(() => counter.write(new Float32Array(3))).toThrow(RangeError);
    counter.write(new Float32Array(0));
    expect(mockNative.syllablesWrite).toHaveBeenCalledTimes(1);
  });

  it('gives null until enough is heard, and a rate after', () => {
    const counter = new SyllableCounter(44100);
    expect(counter.rate()).toBeNull();
    expect(mockNative.syllablesRate).toHaveBeenLastCalledWith(11, 60, 10);
    mockNative.syllablesRate.mockReturnValueOnce(4.2);
    expect(counter.rate(30, 5)).toBe(4.2);
    expect(mockNative.syllablesRate).toHaveBeenLastCalledWith(11, 30, 5);
    expect(() => counter.rate(NaN)).toThrow(RangeError);
    counter.reset();
    expect(mockNative.syllablesReset).toHaveBeenCalledWith(11);
  });

  it('throws once freed, and frees only once', () => {
    const counter = new SyllableCounter(44100);
    counter.free();
    counter.free();
    expect(mockNative.syllablesDestroy).toHaveBeenCalledTimes(1);
    expect(() => counter.rate()).toThrow(/freed/);
  });
});

describe('ListenerTrainer', () => {
  it('has the enum values of the C library', () => {
    expect([TrainerMeasure.Intelligibility, TrainerMeasure.Verification, TrainerMeasure.Retention, TrainerMeasure.Rating]).toEqual([0, 1, 2, 3]);
    expect([TrainerPlan.Steady, TrainerPlan.Ramp, TrainerPlan.Interval, TrainerPlan.Tracking]).toEqual([0, 1, 2, 3]);
    expect([
      TrainerParam.Target, TrainerParam.Margin, TrainerParam.RampStart, TrainerParam.RampStep,
      TrainerParam.RampMinutes, TrainerParam.IntervalSpread, TrainerParam.IntervalMinutes,
      TrainerParam.TrackingGain, TrainerParam.RetentionCost, TrainerParam.TestMax, TrainerParam.TestPrecision,
    ]).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
    expect(TrainerParam.RampMinutes).toBe(4);
  });

  it('checks the seed and passes calls across', () => {
    expect(() => new ListenerTrainer(-1)).toThrow(RangeError);
    expect(() => new ListenerTrainer(1.5)).toThrow(RangeError);
    const trainer = new ListenerTrainer(5);
    expect(mockNative.trainerCreate).toHaveBeenCalledWith(5);
    trainer.setWeight(TrainerMeasure.Rating, 0.1);
    trainer.setParam(TrainerParam.Target, 0.8);
    expect(mockNative.trainerSetWeight).toHaveBeenCalledWith(21, 3, 0.1);
    expect(mockNative.trainerSetParam).toHaveBeenCalledWith(21, 0, 0.8);
    expect([trainer.getWeight(TrainerMeasure.Rating), trainer.getParam(TrainerParam.Target)]).toEqual([0.5, 0.75]);

    trainer.testBegin(10, 0);
    expect(trainer.testRate()).toBe(10);
    expect(trainer.addMeasure(TrainerMeasure.Verification, 0.9, 8, 10, 1)).toBe(true);
    expect(mockNative.trainerAddMeasure).toHaveBeenCalledWith(21, 1, 0.9, 8, 10, 1);
    expect(trainer.testDone).toBe(false);
    expect(trainer.testEnd(2)).toBe(12);
    expect([trainer.threshold, trainer.thresholdLow, trainer.thresholdHigh]).toEqual([12, 10, 14]);

    trainer.sessionBegin(TrainerPlan.Steady, 3);
    expect(mockNative.trainerSessionBegin).toHaveBeenCalledWith(21, 0, 3);
    expect(trainer.sessionRate(4)).toBe(13.2);
    expect(trainer.sessionEnd(1, 5)).toBe(0);
    expect(trainer.addRetention(0, 0.7, 4, 86400, 6)).toBe(true);
    expect(trainer.nextPlan()).toBe(TrainerPlan.Interval);
    expect(trainer.planEffect(TrainerPlan.Ramp)).toBe(0);
    expect(trainer.planEffectSd(TrainerPlan.Ramp)).toBe(0.1);
    expect(trainer.planRetention(TrainerPlan.Ramp)).toBeNaN();
    expect(trainer.planRetentionSd(TrainerPlan.Ramp)).toBeNaN();
    expect(trainer.planSessions(TrainerPlan.Ramp)).toBe(0);
    expect(trainer.planBestProbability(TrainerPlan.Ramp)).toBe(0.25);
    expect([trainer.trend, trainer.trendSd]).toEqual([1000, 1]);
  });

  it('throws once freed', () => {
    const trainer = new ListenerTrainer();
    expect(mockNative.trainerCreate).toHaveBeenCalledWith(0);
    trainer.free();
    trainer.free();
    expect(mockNative.trainerDestroy).toHaveBeenCalledTimes(1);
    expect(() => trainer.threshold).toThrow(/freed/);
  });
});

describe('BlindTrials', () => {
  it('turns -1 and NaN into null', () => {
    const trials = new BlindTrials(1);
    expect(mockNative.trialsCreate).toHaveBeenCalledWith(1);
    expect(trials.next(5)).toBeNull();
    mockNative.trialsNext.mockReturnValueOnce(2);
    expect(trials.next(5)).toEqual({ setting: 2, first: 0, second: 0.5 });
    expect(trials.meanScore(0, 5, 0)).toBeNull();
    mockNative.trialsMeanScore.mockReturnValueOnce(0.6);
    expect(trials.meanScore(0, 5, 0)).toBe(0.6);
    expect(trials.winner(0, 5)).toBeNull();
    mockNative.trialsWinner.mockReturnValueOnce(1);
    expect(trials.winner(0, 5)).toBe(1);
  });

  it('passes calls across and throws once freed', () => {
    const trials = new BlindTrials();
    expect(trials.addSetting()).toBe(0);
    expect(trials.addValue(0, 0.5)).toBe(0);
    trials.setAvailable(0, false);
    expect(mockNative.trialsSetAvailable).toHaveBeenCalledWith(31, 0, false);
    expect(trials.add(0, 5, 0, 0.5, 0.6, 0.8, 1)).toBe(true);
    expect(mockNative.trialsAdd).toHaveBeenCalledWith(31, 0, 5, 0, 0.5, 0.6, 0.8, 1);
    expect([trials.won(0, 5, 0), trials.lost(0, 5, 0), trials.tied(0, 5, 0), trials.heard(0, 5, 0)]).toEqual([2, 1, 0, 3]);
    trials.setConfidence(0.9);
    expect(mockNative.trialsSetConfidence).toHaveBeenCalledWith(31, 0.9);
    trials.free();
    trials.free();
    expect(mockNative.trialsDestroy).toHaveBeenCalledTimes(1);
    expect(() => trials.addSetting()).toThrow(/freed/);
  });
});
