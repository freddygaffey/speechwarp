# speechwarp for JavaScript

Nonlinear speed-up for speech: listen faster and still follow it. This is the JavaScript binding of
[speechwarp](https://github.com/freddygaffey/speechwarp), which packages Google's Speedy algorithm and the
Sonic library, compiled to WebAssembly. It is not an official Google product.

It is one ES module with the WebAssembly inside it (about 60 KB), with TypeScript types. It runs in browsers,
in Node, and inside an AudioWorklet.

```js
import { load } from "speechwarp";

const speechwarp = await load();
const stream = speechwarp.createStream(44100, 2);   // sample rate, channels
stream.speed = 3;

stream.write(samples);             // interleaved Float32Array
play(stream.read());               // everything that is ready

stream.flush();                    // at the end of the input
play(stream.read());
stream.free();
```

- `speed` is the speed you get, within a few percent. `nonlinear = 0` switches to plain, even speed-up and can
  be changed during playback.
- `position` is the input frame being heard. The speed varies from moment to moment, so a player cannot work
  that out by multiplying. Call `reset()` after a seek.
- `writePlanar` and `readPlanar` take one `Float32Array` per channel, as the Web Audio API does.
  `read(array)` and `readPlanar` fill arrays you supply and allocate nothing.
- Counts are in **frames**, not samples.
- Call `free()` when done with a stream. A forgotten one is freed when it is garbage collected.

## In an AudioWorklet

`loadSync()` gets the library ready without `await`, for the constructor of a processor. The package includes
a ready-made processor, `speechwarp/dist/speechwarp-processor.js`, which plays a recording it is sent, and
`example/index.html` is a small player built on it. Its source is short and meant to be copied and changed.

## Building it here

Needs Node and [Emscripten](https://emscripten.org) (`emcc` on the path).

```sh
npm install
npm run build
npm test
```

To try the example, serve this folder (`python3 -m http.server`) and open `example/index.html`.
`example/check.html` renders the worklet offline and reports whether the output is right; it has been run in
Chrome.
