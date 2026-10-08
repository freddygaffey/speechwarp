// Checks the JavaScript half against a stand-in for the native module: argument checking, the frames and
// samples arithmetic, and what is passed across. The native half is exercised by the example app.
import { beforeEach, describe, expect, it, jest } from '@jest/globals';

const mockNative = {
  version: jest.fn(() => '0.3.0'),
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
};
jest.mock('../NativeSpeechwarp', () => ({ __esModule: true, default: mockNative }));

const { Stream, version, MAX_SPEED, MIN_SPEED } = require('../index') as typeof import('../index');

beforeEach(() => {
  jest.clearAllMocks();
});

describe('Stream', () => {
  it('reports the version and limits', () => {
    expect(version()).toBe('0.3.0');
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
