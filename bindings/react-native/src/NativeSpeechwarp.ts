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
  // `buffer` holds 32-bit floats from `byteOffset`. Returns false if memory ran out.
  write(handle: number, buffer: Object, byteOffset: number, frames: number): boolean;
  // Returns the number of frames written to `buffer`.
  read(handle: number, buffer: Object, byteOffset: number, maxFrames: number): number;
  available(handle: number): number;
  flush(handle: number): boolean;
  reset(handle: number): void;
  position(handle: number): number;
}

export default TurboModuleRegistry.getEnforcing<Spec>('Speechwarp');
