# speechwarp-listen

Speech to text on the device with [whisper.cpp](https://github.com/ggml-org/whisper.cpp), from the
[speechwarp](https://github.com/fredgaffey/speechwarp) project. The same calls serve a sentence said back, a whole
audiobook and a live microphone, with times for every segment and word. It is not an official OpenAI product.

```sh
pip install speechwarp-listen
```

## Models

The app downloads a model file once and passes its path; this package never uses the network itself.
`models()` lists those it can offer, with sizes, download addresses and SHA-256 hashes.

```python
import speechwarp_listen as listen

for model in listen.models():
    print(model.id, model.name, model.size_bytes, model.relative_speed)
base = listen.find_model("whisper-base.en")   # download base.download_url to base.file_name
```

## A passage at once

```python
with listen.Transcriber("ggml-base.en.bin") as transcriber:
    transcript = transcriber.transcribe(samples, 44100, language="en", hints=["Hermione", "Quidditch"])
    print(transcript.text)
    for segment in transcript.segments:
        for word in segment.words:
            print(f"{word.start:.2f}-{word.end:.2f} {word.text} ({word.confidence:.0%})")
```

Samples are mono, -1 to 1, at any rate from 4 to 384 kHz, as a NumPy array or anything NumPy can turn into one.
`preset` is `"fast"`, `"balanced"` (the default) or `"accurate"` (beam search, about twice as slow). A
`CancelToken` passed as `cancel` stops a transcription from another thread.

## A book, or a microphone

```python
with transcriber.start_session(44100) as session:
    for chunk in decode_in_chunks(book):        # about 30 s at a time
        session.write(chunk)                     # never waits
        for segment in session.process():        # recognises each complete chunk of 20 to 30 s
            print(f"[{segment.start:.1f}] {segment.text}")
    print(session.finish().text)                 # the rest
```

The session cuts the audio at the quietest moment between 20 and 30 seconds, so that no word is split, and gives
whisper the text before as context. `seconds_recognised / seconds_written` is the progress. For a microphone, write
from the audio callback and call `process()` (and `partial()`, a quick guess at what is being said now) on another
thread: the library allows it, and the calls release the GIL.

## Platforms

Wheels for Linux (x86-64, arm64; glibc), macOS 13.3 or later (Apple silicon and Intel, with the GPU through Metal
when `use_gpu=True`) and Windows (x64, arm64). On x86-64 it needs AVX2 (Intel and AMD processors from 2013 on).
The source distribution builds anywhere with CMake and a C++17 compiler.

## Licences

The package is under the Apache License 2.0; whisper.cpp and ggml, which it contains, are under the MIT licence.
Both are in the wheel's licence files.
