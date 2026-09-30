// An AudioWorkletProcessor that plays a recording through speechwarp. It is shipped in the package as
// dist/speechwarp-processor.js. example/index.html shows how to use it.
//
// processorOptions, all optional: { channels, speed, nonlinear, play } give the recording and settings up
// front, which is the same as sending the messages below first.
//
// Messages in:  { type: "load", channels: Float32Array[] }   the recording, one array per channel
//               { type: "speed", value } { type: "nonlinear", value }
//               { type: "seek", frame } { type: "play" } { type: "pause" }
// Messages out: { type: "position", frame, ended }            about ten times a second
import { loadSync } from "./index.js";

const BLOCK = 128; // frames in one process() call

class SpeechwarpProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const channels = options.outputChannelCount?.[0] ?? 2;
    // sampleRate is a global in an AudioWorklet: the rate of the AudioContext.
    this.stream = loadSync().createStream(sampleRate, channels);
    this.source = [];
    this.next = 0; // the next frame of the source to give to the stream
    this.base = 0; // the source frame at which the stream was last reset
    this.playing = false;
    this.flushed = false;
    this.blocks = 0;
    this.port.onmessage = ({ data }) => this.receive(data);

    const initial = options.processorOptions ?? {};
    if (initial.speed !== undefined) this.stream.speed = initial.speed;
    if (initial.nonlinear !== undefined) this.stream.nonlinear = initial.nonlinear;
    if (initial.channels) this.source = initial.channels;
    this.playing = Boolean(initial.play);
  }

  receive(message) {
    if (message.type === "load") {
      this.source = message.channels;
      this.seek(0);
    } else if (message.type === "speed") {
      this.stream.speed = message.value;
    } else if (message.type === "nonlinear") {
      this.stream.nonlinear = message.value;
    } else if (message.type === "seek") {
      this.seek(message.frame);
    } else if (message.type === "play") {
      this.playing = true;
    } else if (message.type === "pause") {
      this.playing = false;
    }
  }

  seek(frame) {
    this.stream.reset();
    this.base = this.next = Math.max(0, Math.floor(frame));
    this.flushed = false;
  }

  process(inputs, outputs) {
    const output = outputs[0];
    const length = this.source.length ? this.source[0].length : 0;

    if (this.playing && length > 0) {
      // Feed the stream until it has a block ready. At speed s it takes about s blocks of input.
      while (this.stream.available < BLOCK && !this.flushed) {
        if (this.next >= length) {
          this.stream.flush();
          this.flushed = true;
          break;
        }
        const end = Math.min(this.next + 1024, length);
        // The stream may have more channels than the recording; a mono recording goes to all of them.
        const chunk = output.map((_, c) => this.source[Math.min(c, this.source.length - 1)].subarray(this.next, end));
        this.stream.writePlanar(chunk);
        this.next = end;
      }
      this.stream.readPlanar(output); // short at the very end; the rest of the block stays silent
      if (this.flushed && this.stream.available === 0) this.playing = false;
    }

    if (++this.blocks % 32 === 0) {
      this.port.postMessage({
        type: "position",
        frame: this.base + this.stream.position,
        ended: this.flushed && this.stream.available === 0,
      });
    }
    return true;
  }
}

registerProcessor("speechwarp-processor", SpeechwarpProcessor);
