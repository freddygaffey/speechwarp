"""Speech to text on the device with whisper.cpp, for speechwarp.

A passage given at once (a sentence said back, a clip)::

    import speechwarp_listen as listen

    with listen.Transcriber("ggml-base.en.bin") as transcriber:
        transcript = transcriber.transcribe(samples, 44100, language="en")
        print(transcript.text)
        for segment in transcript.segments:
            print(segment.start, segment.end, segment.text)

Audio that arrives over time (a book decoded in chunks, a microphone), of any length::

    with transcriber.start_session(44100) as session:
        for chunk in chunks:
            session.write(chunk)          # never waits for recognition
            for segment in session.process():
                print(segment.text)
        print(session.finish().text)      # everything not yet returned

Samples are mono, -1 to 1, at any rate from 4000 to 384000 Hz, as a NumPy array or anything NumPy can turn into one.
The app downloads the model file; :func:`models` lists those it can offer, with sizes and SHA-256 hashes.

The calls into the library release the GIL, so one thread may write to a session while another processes it.
"""
import ctypes
import os
import threading
from dataclasses import dataclass, field
from typing import Iterable, List, Optional, Sequence

import numpy as np

from . import _native as _n

__all__ = [
    "Cancelled", "CancelToken", "ListenError", "Model", "Segment", "Session", "Transcriber", "Transcript", "Word",
    "engine_version", "find_model", "model_for_file", "models", "native_version", "set_engine_log",
    "system_info",
]

_PRESETS = {"fast": _n.PRESET_FAST, "balanced": _n.PRESET_BALANCED, "accurate": _n.PRESET_ACCURATE}


class ListenError(Exception):
    """Something the library refused or failed to do. ``code`` is one of its error codes (negative)."""

    def __init__(self, code: int, message: Optional[str] = None):
        super().__init__(message or _n.error_message(code).decode())
        self.code = code


class Cancelled(ListenError):
    """The work was cancelled. Nothing was lost: a session can carry on."""

    def __init__(self):
        super().__init__(_n.ERROR_CANCELLED)


def _check(code: int) -> int:
    if code == _n.ERROR_CANCELLED:
        raise Cancelled()
    if code == _n.ERROR_MEMORY:
        raise MemoryError(_n.error_message(code).decode())
    if code < 0:
        raise ListenError(code)
    return code


# ---- Results

@dataclass(frozen=True)
class Word:
    """A word, with its time in seconds from the start of what was given."""

    text: str
    start: float
    end: float
    #: How sure the recogniser is, 0 to 1.
    confidence: float


@dataclass(frozen=True)
class Segment:
    """A stretch of recognised speech, usually a phrase or sentence. ``words`` is empty without word times."""

    text: str
    start: float
    end: float
    words: List[Word] = field(default_factory=list)


@dataclass(frozen=True)
class Transcript:
    """What was recognised."""

    segments: List[Segment]

    @property
    def text(self) -> str:
        """All the segments' text, joined with spaces."""
        return " ".join(segment.text.strip() for segment in self.segments)


def _segments(result) -> List[Segment]:
    """The segments of a native result, which is then freed."""
    if not result:
        return []
    try:
        segments = []
        for s in range(_n.result_segment_count(result)):
            words = [Word(_n.result_word_text(result, s, w).decode("utf-8", "replace"),
                          _n.result_word_start(result, s, w), _n.result_word_end(result, s, w),
                          _n.result_word_probability(result, s, w))
                     for w in range(_n.result_word_count(result, s))]
            segments.append(Segment(_n.result_segment_text(result, s).decode("utf-8", "replace"),
                                    _n.result_segment_start(result, s), _n.result_segment_end(result, s), words))
        return segments
    finally:
        _n.result_free(result)


# ---- Models

@dataclass(frozen=True)
class Model:
    """A whisper.cpp model an app can download, or a model file it has."""

    #: Stable identifier, such as "whisper-base.en".
    id: str
    #: A name to show, such as "Whisper base (English)".
    name: str
    #: BCP 47 tags it understands; empty means many (about a hundred, detected or chosen).
    languages: List[str]
    #: Download size in bytes.
    size_bytes: int
    #: Where to download it, or None for a file not in the catalogue.
    download_url: Optional[str]
    #: Lower-case hex SHA-256 of the download, or None.
    sha256: Optional[str]
    #: Rough time to transcribe against the fastest model, which is 1; NaN for a file not in the catalogue.
    relative_speed: float
    #: The name to save the download under, such as "ggml-base.en.bin".
    file_name: str
    #: Always "whisper", to match the engine-independent interface of the C# and Swift packages.
    engine: str = "whisper"


def _catalogue() -> List[Model]:
    found = []
    for i in range(_n.catalogue_count()):
        languages = _n.catalogue_languages(i).decode()
        found.append(Model(
            id=_n.catalogue_id(i).decode(), name=_n.catalogue_name(i).decode(),
            languages=[tag.strip() for tag in languages.split(",") if tag.strip()],
            size_bytes=_n.catalogue_size(i), download_url=_n.catalogue_url(i).decode(),
            sha256=_n.catalogue_sha256(i).decode(), relative_speed=_n.catalogue_relative_speed(i),
            file_name=_n.catalogue_file_name(i).decode()))
    return found


_MODELS = _catalogue()


def models() -> List[Model]:
    """Every model in the catalogue, roughly from least to most accurate. The order may change; keep the id.

    The ".en" models understand English only and are a little more accurate at it. The compressed (quantised, q5)
    models are well under half the size of the full ones, for a little accuracy.
    """
    return list(_MODELS)


def find_model(id: str) -> Optional[Model]:
    """The catalogue model with this id, such as "whisper-base.en", or None."""
    return next((model for model in _MODELS if model.id == id), None)


def model_for_file(path: str) -> Model:
    """The catalogue model whose file name matches the file at ``path``, or a description of any other model file:
    id "whisper-file:" and the file name, languages unknown (empty), no download, relative speed NaN."""
    name = os.path.basename(path)
    for model in _MODELS:
        if model.file_name.lower() == name.lower():
            return model
    size = os.path.getsize(path) if os.path.isfile(path) else 0
    return Model(id=f"whisper-file:{name}", name=name, languages=[], size_bytes=size, download_url=None, sha256=None,
                 relative_speed=float("nan"), file_name=name)


def native_version() -> str:
    """The version of the library, the same as the package's, such as "0.3.7"."""
    return _n.version().decode()


def engine_version() -> str:
    """The version of whisper.cpp in the library, such as "1.9.5"."""
    return _n.engine_version().decode()


def system_info() -> str:
    """What the library can use on this machine (CPU features, GPU), as one line, for diagnostics."""
    return _n.system_info().decode()


def set_engine_log(on: bool) -> None:
    """Send whisper.cpp's own log to standard error (True) or nowhere (False, the default). For the whole process."""
    _n.set_log(1 if on else 0)


# ---- Options and cancelling

def _samples(samples) -> np.ndarray:
    array = np.ascontiguousarray(samples, dtype=np.float32)
    if array.ndim != 1:
        raise ValueError("samples must be mono: a one-dimensional array")
    return array


def _check_rate(sample_rate: int) -> int:
    if not 4000 <= int(sample_rate) <= 384000:
        raise ValueError(f"the sample rate must be 4000 to 384000 Hz, not {sample_rate}")
    return int(sample_rate)


class _Options:
    """Native options, made from keyword arguments."""

    def __init__(self, language: Optional[str] = None, word_timestamps: bool = True, hints: Iterable[str] = (),
                 preset: str = "balanced", threads: int = 0):
        if preset not in _PRESETS:
            raise ValueError(f"preset must be one of {', '.join(_PRESETS)}, not {preset!r}")
        if threads < 0:
            raise ValueError("threads must not be negative")
        self.handle = _n.options_create()
        if not self.handle:
            raise MemoryError("options could not be created")
        try:
            if _n.options_set_language(self.handle, language.encode() if language else None) != _n.OK:
                raise ValueError(f"whisper does not know the language {language!r}")
            _n.options_set_word_timestamps(self.handle, 1 if word_timestamps else 0)
            hints = [hint for hint in hints if hint]
            if hints:
                # Whisper takes the prompt as the text spoken just before, so a list of words separated by commas works.
                _n.options_set_prompt(self.handle, ", ".join(hints).encode())
            _n.options_set_preset(self.handle, _PRESETS[preset])
            _n.options_set_threads(self.handle, int(threads))
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            _n.options_free(self.handle)
            self.handle = None


class CancelToken:
    """Stops a :meth:`Transcriber.transcribe` from another thread: pass it as ``cancel``, then call :meth:`cancel`.
    The transcription raises :class:`Cancelled` soon after. Once cancelled it stays cancelled."""

    def __init__(self):
        self._lock = threading.Lock()
        self._cancelled = False
        self._running = set()

    @property
    def cancelled(self) -> bool:
        return self._cancelled

    def cancel(self) -> None:
        with self._lock:
            self._cancelled = True
            for handle in self._running:
                _n.options_cancel(handle)

    def _enter(self, handle) -> None:
        with self._lock:
            if self._cancelled:
                _n.options_cancel(handle)
            self._running.add(handle)

    def _leave(self, handle) -> None:
        with self._lock:
            self._running.discard(handle)


# ---- The transcriber

class Transcriber:
    """Speech to text with one whisper.cpp model file.

    ``threads`` is the default number of CPU threads (0 for the number of cores, at most 8). ``use_gpu`` uses the GPU
    where the library has one (Metal, on macOS); elsewhere it is ignored. The model is loaded by :meth:`prepare`, or
    by the first transcription. A transcriber may be used from any number of threads at once; each transcription and
    session has its own working memory. Close it (or use ``with``) to free the model; sessions still open keep it.
    """

    def __init__(self, model_path: str, model: Optional[Model] = None, threads: int = 0, use_gpu: bool = False):
        if model is not None and model.engine != "whisper":
            raise ValueError(f"{model.id} is not a whisper model")
        self.model_path = os.fspath(model_path)
        self.model = model or model_for_file(self.model_path)
        self.threads = max(0, int(threads))
        self.use_gpu = bool(use_gpu)
        self._lock = threading.Lock()
        self._handle = None
        self._users = 0
        self._closed = False

    @property
    def is_ready(self) -> bool:
        """Whether the model has been loaded."""
        return self._handle is not None

    @property
    def is_multilingual(self) -> Optional[bool]:
        """Whether the model understands many languages (False for ".en" models), or None before it is loaded."""
        handle = self._handle
        return None if handle is None else bool(_n.model_multilingual(handle))

    def prepare(self) -> "Transcriber":
        """Load the model, if it is not loaded: from a fraction of a second (tiny) to several seconds (large).

        Raises FileNotFoundError for a missing file and ListenError for a file that is not a whisper.cpp model."""
        with self._lock:
            if self._closed:
                raise ValueError("the transcriber is closed")
            if self._handle is None:
                if not os.path.isfile(self.model_path):
                    raise FileNotFoundError(self.model_path)
                handle = _n.model_load(os.fsencode(self.model_path), self.threads, _n.LOAD_GPU if self.use_gpu else 0)
                if not handle:
                    raise ListenError(_n.ERROR_ARGUMENT,
                                      f"{self.model_path} is not a whisper.cpp model, or memory ran out")
                self._handle = handle
        return self

    def _acquire(self):
        self.prepare()
        with self._lock:
            self._users += 1
            return self._handle

    def _release(self):
        with self._lock:
            self._users -= 1
            self._free_if_done()

    def _free_if_done(self):
        if self._closed and self._users == 0 and self._handle is not None:
            _n.model_free(self._handle)
            self._handle = None

    def transcribe(self, samples, sample_rate: int, *, language: Optional[str] = None, word_timestamps: bool = True,
                   hints: Sequence[str] = (), preset: str = "balanced", threads: int = 0,
                   cancel: Optional[CancelToken] = None) -> Transcript:
        """Transcribe a whole passage given at once: a sentence said back, a clip.

        ``language`` is a BCP 47 tag ("en", "en-GB") or None to detect it. ``hints`` are words likely to appear
        (names, invented words). ``preset`` is "fast", "balanced" or "accurate" (beam search, about twice as slow).
        The audio and a 16 kHz copy are held in memory, so use a session for more than a few minutes. Blocks until
        done, with the GIL released; ``cancel`` stops it from another thread.
        """
        sample_rate = _check_rate(sample_rate)
        audio = _samples(samples)
        options = _Options(language, word_timestamps, hints, preset, threads)
        model = self._acquire()
        try:
            if audio.size == 0:
                return Transcript([])
            if cancel is not None:
                cancel._enter(options.handle)
            error = ctypes.c_int(_n.OK)
            try:
                result = _n.transcribe(model, audio.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), audio.size,
                                       sample_rate, options.handle, ctypes.byref(error))
            finally:
                if cancel is not None:
                    cancel._leave(options.handle)
            if not result:
                _check(error.value if error.value < 0 else _n.ERROR_ENGINE)
            return Transcript(_segments(result))
        finally:
            options.close()
            self._release()

    def start_session(self, sample_rate: int, *, language: Optional[str] = None, word_timestamps: bool = True,
                      hints: Sequence[str] = (), preset: str = "balanced", threads: int = 0) -> "Session":
        """Start a session for audio that arrives over time: a book decoded in chunks, or a microphone. Any length.
        Loads the model first if needed. The options are as for :meth:`transcribe`."""
        sample_rate = _check_rate(sample_rate)
        options = _Options(language, word_timestamps, hints, preset, threads)
        model = self._acquire()
        try:
            handle = _n.session_create(model, sample_rate, options.handle)
        finally:
            options.close()
        if not handle:
            self._release()
            raise MemoryError("the session could not be created")
        return Session(handle, self)

    def close(self) -> None:
        """Free the model once every session on it has been closed."""
        with self._lock:
            self._closed = True
            self._free_if_done()

    def __enter__(self) -> "Transcriber":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


# ---- Sessions

class Session:
    """Audio in over time, recognised text out as it is ready. Made by :meth:`Transcriber.start_session`.

    :meth:`write` only stores audio and never waits. :meth:`process` recognises it in chunks of 20 to 30 seconds, each
    cut at the quietest moment so that no word is split, and returns the segments it finished. Audio waiting to be
    recognised is held at 16 kHz (about 230 MB an hour), so a caller with a whole book should write about 30 seconds
    at a time and process between writes; for a microphone, write from the audio callback and process on another
    thread. :meth:`write`, :meth:`take`, :meth:`cancel` and the seconds may be called from any thread at any time;
    :meth:`process`, :meth:`partial` and :meth:`finish` take turns. Close the session (or use ``with``) when done.
    """

    def __init__(self, handle, transcriber: Transcriber):
        self._handle = handle
        self._transcriber = transcriber

    def _live(self):
        if not self._handle:
            raise ValueError("the session is closed")
        return self._handle

    def write(self, samples) -> None:
        """Add mono samples, -1 to 1, of any length. Raises ListenError after :meth:`finish`."""
        audio = _samples(samples)
        if audio.size:
            _check(_n.session_write(self._live(), audio.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), audio.size))

    def process(self) -> List[Segment]:
        """Recognise every complete chunk written so far and return the segments finished (often none: a chunk
        completes every 20 to 30 s). Raises :class:`Cancelled` if :meth:`cancel` stopped it; nothing is lost."""
        handle = self._live()
        _check(_n.session_process(handle))
        return self.take()

    def take(self) -> List[Segment]:
        """The segments finished and not yet returned, with times from the start of the session."""
        result = _n.session_take(self._live())
        if not result:
            raise MemoryError("the segments could not be collected")
        return _segments(result)

    def partial(self) -> Optional[Segment]:
        """A quick guess at the audio not yet in a finished segment, for live display, or None. It uses the fast
        preset, takes a fraction of a second to a few seconds, and changes nothing in the session."""
        error = ctypes.c_int(_n.OK)
        result = _n.session_partial(self._live(), ctypes.byref(error))
        if not result:
            _check(error.value if error.value < 0 else _n.ERROR_ENGINE)
        segments = _segments(result)
        if not segments:
            return None
        if len(segments) == 1:
            return segments[0]
        return Segment(" ".join(s.text for s in segments), segments[0].start, segments[-1].end,
                       [word for s in segments for word in s.words])

    def finish(self) -> Transcript:
        """End the input, recognise the rest, and return every segment not yet returned. If :meth:`cancel` stops it,
        it raises :class:`Cancelled` and can be called again to carry on."""
        _check(_n.session_finish(self._live()))
        return Transcript(self.take())

    def cancel(self) -> None:
        """Make the :meth:`process`, :meth:`partial` or :meth:`finish` running now (or the next one) stop soon."""
        _n.session_cancel(self._live())

    @property
    def seconds_written(self) -> float:
        """Seconds of audio written so far."""
        return _n.session_seconds_written(self._live())

    @property
    def seconds_recognised(self) -> float:
        """Seconds of the audio written that have been recognised; the ratio to :attr:`seconds_written` is the
        progress."""
        return _n.session_seconds_recognised(self._live())

    def close(self) -> None:
        """Free the session. It must not be in use on another thread."""
        if self._handle:
            _n.session_free(self._handle)
            self._handle = None
            self._transcriber._release()

    def __enter__(self) -> "Session":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
