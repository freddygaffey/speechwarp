# Examples

Each of these runs. Where it needs a recording, any 16-bit PCM WAV file of speech will do.

| Example | Language | What it shows |
|---------|----------|---------------|
| [`c/player.c`](c/player.c) | C | The loop of a real-time player: feeding on demand, seeking, changing speed and mode, reporting the position. Needs no audio file or sound card. Built and run by `ctest`. |
| [`../tools/speechwarp.c`](../tools/speechwarp.c) | C | The command-line tool: converting a WAV file, streaming it through. |
| [`python/speed_up_wav.py`](python/speed_up_wav.py) | Python | Converting a WAV file in one call. |
| [`python/streaming.py`](python/streaming.py) | Python | Audio in pieces, with the position. Needs no audio file. |
| [`dotnet/SpeedUpWav`](dotnet/SpeedUpWav/) | C# | Converting a WAV file, streaming it through. |
| [`swift`](swift/) | Swift | Converting any audio file the system can read, with AVFoundation. |
| [`android/SpeechPlayer.kt`](android/SpeechPlayer.kt) | Kotlin | The playback thread of a player with `AudioTrack`, with locking. A sketch: it compiles but has not been run on a device. |
| [`node/speed-up-wav.mjs`](node/speed-up-wav.mjs) | JavaScript | Converting a WAV file in Node. |
| [`../bindings/js/example/`](../bindings/js/example/) | JavaScript | A browser player in an AudioWorklet. |
| [`blind-ab-test`](blind-ab-test/) | Python, browser | A blind listening test of even against nonlinear speed-up, on your own recordings. |

## Running them

From the top of the repository:

```sh
# C
cmake -B build && cmake --build build
build/examples/c/player
build/speechwarp --speed 3 talk.wav talk-3x.wav

# Python
pip install .
python examples/python/streaming.py
python examples/python/speed_up_wav.py talk.wav talk-3x.wav --speed 3

# C#
bindings/dotnet/build-native.sh
dotnet run --project examples/dotnet/SpeedUpWav -- talk.wav talk-3x.wav 3

# Swift
(cd examples/swift && swift run speedup talk.wav talk-3x.wav 3)

# JavaScript (needs Emscripten to build the package)
(cd bindings/js && npm install && npm run build)
node examples/node/speed-up-wav.mjs talk.wav talk-3x.wav 3
```
