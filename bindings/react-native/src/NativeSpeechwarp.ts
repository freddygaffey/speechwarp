import { TurboModuleRegistry, type TurboModule } from 'react-native';

// The native module. Use the Stream class in index.tsx, not this.
//
// Audio crosses as an ArrayBuffer, which the native side reads and writes in place: no copying and no
// conversion to arrays of numbers. The spec language has no ArrayBuffer type, so it is declared as Object.
export interface Spec extends TurboModule {
  version(): string;
  // Returns a handle, or 0 if the stream could not be created.
  // Not called `create`: that name is taken by a method every native module inherits.
  createStream(sampleRate: number, channels: number): number;
  destroyStream(handle: number): void;
  setSpeed(handle: number, speed: number): void;
  getSpeed(handle: number): number;
  setNonlinear(handle: number, amount: number): void;
  getNonlinear(handle: number): number;
  setPauseCap(handle: number, value: number): void;
  getPauseCap(handle: number): number;
  setSpeedFloor(handle: number, value: number): void;
  getSpeedFloor(handle: number): number;
  setRhythmGap(handle: number, value: number): void;
  getRhythmGap(handle: number): number;
  setRhythmRate(handle: number, value: number): void;
  getRhythmRate(handle: number): number;
  setKeepSpeed(handle: number, enabled: boolean): void;
  getKeepSpeed(handle: number): boolean;
  // Negative until enough has been heard.
  syllableRate(handle: number): number;
  // `buffer` holds 32-bit floats from `byteOffset`. Returns false if memory ran out.
  write(handle: number, buffer: Object, byteOffset: number, frames: number): boolean;
  // Returns the number of frames written to `buffer`.
  read(handle: number, buffer: Object, byteOffset: number, maxFrames: number): number;
  available(handle: number): number;
  flush(handle: number): boolean;
  reset(handle: number): void;
  position(handle: number): number;

  // Handles of syllable counters, trainers and trial designers are separate from stream handles. create
  // returns 0 if out of memory. Seeds are unsigned 64-bit in the C library; here, whole numbers up to 2^53.
  syllablesCreate(sampleRate: number, channels: number): number;
  syllablesDestroy(handle: number): void;
  // `buffer` holds 32-bit floats (syllablesWrite) or 16-bit integers (syllablesWriteI16) from `byteOffset`.
  syllablesWrite(handle: number, buffer: Object, byteOffset: number, frames: number): boolean;
  syllablesWriteI16(handle: number, buffer: Object, byteOffset: number, frames: number): boolean;
  trainerCreate(seed: number): number;
  trainerDestroy(handle: number): void;
  trialsCreate(seed: number): number;
  trialsDestroy(handle: number): void;

  // stream
  setHeardPause(handle: number, seconds: number, fromSpeed: number): void;
  getHeardPause(handle: number): number;
  getHeardPauseFrom(handle: number): number;
  setFloorBlend(handle: number, fraction: number, fromSpeed: number, fullSpeed: number): void;
  getFloorBlend(handle: number): number;
  getFloorBlendFrom(handle: number): number;
  getFloorBlendFull(handle: number): number;
  // counter
  syllablesRate(handle: number, windowSeconds: number, minimumSeconds: number): number;
  syllablesReset(handle: number): void;
  // trainer
  trainerSetWeight(handle: number, kind: number, weight: number): void;
  trainerGetWeight(handle: number, kind: number): number;
  trainerSetParam(handle: number, param: number, value: number): void;
  trainerGetParam(handle: number, param: number): number;
  trainerAddMeasure(handle: number, kind: number, score: number, items: number, rate: number, time: number): boolean;
  trainerTestBegin(handle: number, priorRate: number, time: number): void;
  trainerTestRate(handle: number): number;
  trainerTestDone(handle: number): boolean;
  trainerTestEnd(handle: number, time: number): number;
  trainerThreshold(handle: number): number;
  trainerThresholdLow(handle: number): number;
  trainerThresholdHigh(handle: number): number;
  trainerSessionBegin(handle: number, plan: number, time: number): void;
  trainerSessionRate(handle: number, time: number): number;
  trainerSessionEnd(handle: number, listeningHours: number, time: number): number;
  trainerAddRetention(handle: number, session: number, score: number, items: number, delaySeconds: number, time: number): boolean;
  trainerNextPlan(handle: number): number;
  trainerPlanEffect(handle: number, plan: number): number;
  trainerPlanEffectSd(handle: number, plan: number): number;
  trainerPlanRetention(handle: number, plan: number): number;
  trainerPlanRetentionSd(handle: number, plan: number): number;
  trainerPlanSessions(handle: number, plan: number): number;
  trainerPlanBestProbability(handle: number, plan: number): number;
  trainerTrend(handle: number): number;
  trainerTrendSd(handle: number): number;
  // trials
  trialsAddSetting(handle: number): number;
  trialsAddValue(handle: number, setting: number, value: number): number;
  trialsSetAvailable(handle: number, setting: number, available: boolean): void;
  trialsAdd(handle: number, setting: number, speed: number, firstValue: number, secondValue: number, firstScore: number, secondScore: number, preferred: number): boolean;
  trialsNext(handle: number, speed: number): number;
  trialsNextFirst(handle: number): number;
  trialsNextSecond(handle: number): number;
  trialsWon(handle: number, setting: number, speed: number, value: number): number;
  trialsLost(handle: number, setting: number, speed: number, value: number): number;
  trialsTied(handle: number, setting: number, speed: number, value: number): number;
  trialsHeard(handle: number, setting: number, speed: number, value: number): number;
  trialsMeanScore(handle: number, setting: number, speed: number, value: number): number;
  trialsWinner(handle: number, setting: number, speed: number): number;
  trialsSetConfidence(handle: number, confidence: number): void;
  // word scoring: [share, right, missed, wrong, extra]
  scoreWords(reference: string, heard: string): Array<number>;
}

export default TurboModuleRegistry.getEnforcing<Spec>('Speechwarp');
