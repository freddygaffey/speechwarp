# speechwarp_listen: speech to text with whisper.cpp

An optional module beside speechwarp that writes down what is said, using
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) on the CPU. It is for three jobs, with the same calls
for all of them:

- **checking a sentence said back** (the listener trainer): one call on a few seconds of audio;
- **transcribing whole audiobooks** with timestamps: a session fed the decoded audio a chunk at a time, for
  hours, in flat memory;
- **live microphone input**: the same session, with a quick guess at the words not yet finished.

It is a separate library, `speechwarp_listen`, with its own plain C API in
[`include/speechwarp_listen.h`](include/speechwarp_listen.h), where every function is documented. The
speechwarp library, its WebAssembly build and its bindings do not include it and do not grow. Its version is
speechwarp's.

## Building

whisper.cpp is a git submodule that is not fetched by default (so that nobody who only wants speechwarp
downloads it). Fetch it, then configure this directory on its own:

    git submodule update --init --checkout third_party/whisper.cpp
    cmake -S listen -B build-listen -G Ninja
    cmake --build build-listen
    ctest --test-dir build-listen

That builds:

| Output | What it is |
|---|---|
| `libspeechwarp_listen.so` / `.dylib` / `.dll` | Shared library with whisper.cpp and ggml inside. Exports only `speechwarp_listen_*`. |
| `libspeechwarp_listen.a` | Static library. On Apple and Linux, whisper.cpp and ggml are linked into it and made local, so only `speechwarp_listen_*` is global and it links beside another copy of whisper.cpp or ggml. A program linking it needs the C++ runtime and, on Apple, `-framework Accelerate -framework Foundation`. With MSVC it is a plain static library that needs whisper.cpp's libraries too. |
| `speechwarp-transcribe` | A command-line tool, below. |

Options (all `-D...=ON/OFF`):

| Option | Default | |
|---|---|---|
| `SPEECHWARP_LISTEN_BUILD_SHARED`, `_STATIC` | ON | Which libraries to build. |
| `SPEECHWARP_LISTEN_METAL` | OFF | Use the GPU through Metal on Apple platforms (shaders embedded). Then pass `SPEECHWARP_LISTEN_LOAD_GPU` when loading a model. Not yet tested. |
| `SPEECHWARP_LISTEN_BUILD_TOOLS`, `_TESTS` | ON when top level | |
| `GGML_NATIVE` | ON | whisper.cpp's: optimise for this machine's CPU. **Turn it off for anything you ship** to other machines. |

OpenMP is off, so the library needs no OpenMP runtime; ggml uses its own thread pool.

## Models

The app downloads a model file and passes its path. The library knows the usual ones (the catalogue
functions give each one's id, name, languages, size, URL, SHA-256 and rough relative speed), all from
[ggerganov/whisper.cpp on Hugging Face](https://huggingface.co/ggerganov/whisper.cpp):

| Id | Size | Languages |
|---|---|---|
| `whisper-tiny.en`, `whisper-tiny` | 78 MB | English; many |
| `whisper-base.en`, `whisper-base` | 148 MB | English; many |
| `whisper-small.en-q5_1` | 190 MB | English (compressed) |
| `whisper-small.en`, `whisper-small` | 488 MB | English; many |
| `whisper-medium.en-q5_0`, `whisper-medium-q5_0` | 539 MB | English; many (compressed) |
| `whisper-medium.en`, `whisper-medium` | 1.5 GB | English; many |
| `whisper-large-v3-turbo-q5_0` | 574 MB | many (compressed) |
| `whisper-large-v3-turbo` | 1.6 GB | many |

Check the SHA-256 after downloading. The relative speeds are estimates for choosing, not measurements. The
English-only models are a little more accurate for English at the same size. Any other whisper.cpp ggml
model file loads too.

## Using it

A sentence said back (one call; the samples may be at any rate from 4 to 384 kHz and are resampled to
16 kHz inside):

```c
speechwarp_listen_model* model = speechwarp_listen_model_load("ggml-base.en.bin", 0, 0);
speechwarp_listen_options* options = speechwarp_listen_options_create();
speechwarp_listen_options_set_language(options, "en");
int error;
speechwarp_listen_result* result = speechwarp_listen_transcribe(model, samples, count, 44100, options, &error);
printf("%s\n", speechwarp_listen_result_text(result));
for (int w = 0; w < speechwarp_listen_result_word_count(result, 0); w++)
  printf("%s %.2f\n", speechwarp_listen_result_word_text(result, 0, w),
         speechwarp_listen_result_word_probability(result, 0, w));
speechwarp_listen_result_free(result);
```

A book or a microphone (a session; one thread may write while another processes):

```c
speechwarp_listen_session* session = speechwarp_listen_session_create(model, 44100, options);
while (decode(chunk, &frames)) {
  speechwarp_listen_session_write(session, chunk, frames);   /* cheap: never recognises */
  if (speechwarp_listen_session_process(session) > 0) {      /* recognises every complete chunk */
    speechwarp_listen_result* done = speechwarp_listen_session_take(session);
    /* ... segments with times from the start of the session ... */
    speechwarp_listen_result_free(done);
  }
}
speechwarp_listen_session_finish(session);                   /* the rest */
/* take once more, then */
speechwarp_listen_session_free(session);
```

How a session works: audio is resampled to 16 kHz as it is written. `process` recognises it in chunks of 20
to 30 seconds, each cut at the quietest 50 ms in that range so that no word is split, and gives whisper the
previous chunk's text as context, with its timestamps, as whisper does between its own 30-second windows, so
that sentences carry on across cuts. A segment longer than 10 seconds (whisper sometimes gives a whole chunk
as one) is split at the ends of its sentences. On a 2.5-minute two-voice recording the session's text
differs from a single call's by about 1.5% of words. `partial` runs the fast
preset on the unfinished tail for live display without changing anything. Audio not yet recognised is held
in memory (about 230 MB an hour), so a book reader should process as it decodes, as `speechwarp-transcribe`
does, rather than write the whole book first.

Presets: `FAST` is greedy decoding with no retries; `BALANCED` (the default) retries at higher temperatures
when the text looks wrong, as whisper does; `ACCURATE` is beam search with five beams. Word times come from
whisper's token timestamps: good to a tenth of a second or two, not exact.

Threads: a model may be shared; any number of transcriptions and sessions may run on it at once, each with
its own working memory. Cancel a call with `speechwarp_listen_options_cancel` or
`speechwarp_listen_session_cancel` from another thread. The header says what may overlap with what.

## The command-line tool

    speechwarp-transcribe ggml-base.en.bin talk.wav                 # text, a line per segment
    speechwarp-transcribe ggml-base.en.bin talk.wav --srt > talk.srt
    speechwarp-transcribe ggml-base.en.bin talk.wav --words         # with each word's times and probability
    speechwarp-transcribe ggml-small.bin talk.wav --language de --preset accurate

It reads PCM and float WAV, mixes to mono, and streams the file through a session 30 seconds at a time, so
memory stays flat for a file of any length (`--once` sends it all in one call instead). `--help` lists the
rest.

## Tests

`ctest` runs the catalogue, resampler and symbol checks anywhere. The tests that recognise speech need a
model and speech: set `SPEECHWARP_LISTEN_MODEL` to a model file (`ggml-tiny.en.bin` is enough and takes
about a minute), and run on macOS, where the speech is made with `say`. Otherwise they are skipped. They check
the words of a sentence, that times are in order and within the audio, that a session over a 2.5-minute
recording gives the same text as one call (within 10% word error rate, about 1.5% in practice), cancelling,
and that one thread can write while another processes. No model or audio is kept in the repository.

## Licence

This module is Apache-2.0, like speechwarp. whisper.cpp and ggml, built into the library, are under the MIT
licence (`third_party/whisper.cpp/LICENSE`): ship that notice with any binary. The models are OpenAI's
Whisper weights (MIT), converted by the whisper.cpp project.
