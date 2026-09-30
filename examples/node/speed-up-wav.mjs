// Speed up a 16-bit PCM WAV file with Node.
//
//     cd bindings/js && npm install && npm run build
//     node ../../examples/node/speed-up-wav.mjs talk.wav talk-3x.wav 3
//
// In your own project, import from "speechwarp" instead of the path below.
import { readFileSync, writeFileSync } from "node:fs";

import { load } from "../../bindings/js/dist/index.js";

const [input, output, speedText = "2"] = process.argv.slice(2);
if (!input || !output) {
  console.error("usage: node speed-up-wav.mjs input.wav output.wav [speed]");
  process.exit(2);
}

// A minimal WAV reader: find the "fmt " and "data" chunks.
const file = readFileSync(input);
let sampleRate = 0, channels = 0, bits = 0, data;
for (let at = 12; at + 8 <= file.length; ) {
  const id = file.toString("latin1", at, at + 4);
  const size = file.readUInt32LE(at + 4);
  if (id === "fmt ") {
    channels = file.readUInt16LE(at + 10);
    sampleRate = file.readUInt32LE(at + 12);
    bits = file.readUInt16LE(at + 22);
  } else if (id === "data") {
    data = file.subarray(at + 8, at + 8 + size);
  }
  at += 8 + size + (size & 1);
}
if (!data || bits !== 16) {
  console.error("this example reads 16-bit PCM WAV only");
  process.exit(1);
}

// speechwarp takes floats from -1 to 1, with the channels interleaved as they are in the file.
const samples = new Float32Array(data.length / 2);
for (let i = 0; i < samples.length; i++) samples[i] = data.readInt16LE(2 * i) / 32768;

const speechwarp = await load();
const stream = speechwarp.createStream(sampleRate, channels);
stream.speed = Number(speedText);
stream.write(samples);
stream.flush();
const fast = stream.read();
stream.free();

const out = Buffer.alloc(44 + fast.length * 2);
out.write("RIFF", 0);
out.writeUInt32LE(36 + fast.length * 2, 4);
out.write("WAVEfmt ", 8);
out.writeUInt32LE(16, 16);
out.writeUInt16LE(1, 20);
out.writeUInt16LE(channels, 22);
out.writeUInt32LE(sampleRate, 24);
out.writeUInt32LE(sampleRate * channels * 2, 28);
out.writeUInt16LE(channels * 2, 32);
out.writeUInt16LE(16, 34);
out.write("data", 36);
out.writeUInt32LE(fast.length * 2, 40);
for (let i = 0; i < fast.length; i++) {
  out.writeInt16LE(Math.round(Math.max(-1, Math.min(1, fast[i])) * 32767), 44 + 2 * i);
}
writeFileSync(output, out);

const seconds = (n) => (n / channels / sampleRate).toFixed(1);
console.log(`${seconds(samples.length)} s in, ${seconds(fast.length)} s out, ${(samples.length / fast.length).toFixed(2)}x`);
