"""Tests of the Python package. Those that recognise speech need a whisper.cpp model, named by the environment
variable SPEECHWARP_LISTEN_MODEL (ggml-tiny.en.bin is enough), and are skipped without one. The speech is the sample
that comes with whisper.cpp (third_party/whisper.cpp/samples/jfk.wav), so the submodule must be checked out."""
import math
import os
import re
import threading
import time
import wave
from pathlib import Path

import numpy as np
import pytest

import speechwarp_listen as listen

ROOT = Path(__file__).resolve().parents[3]
SPEECH = ROOT / "third_party" / "whisper.cpp" / "samples" / "jfk.wav"


def plain(text):
    """Lower case, no punctuation, single spaces."""
    return " ".join(re.sub(r"[^a-z0-9' ]", " ", text.lower()).split())


@pytest.fixture(scope="module")
def model_path():
    path = os.environ.get("SPEECHWARP_LISTEN_MODEL")
    if not path:
        pytest.skip("set SPEECHWARP_LISTEN_MODEL to a whisper.cpp model to run this test")
    assert os.path.isfile(path), f"SPEECHWARP_LISTEN_MODEL names a missing file: {path}"
    return path


@pytest.fixture(scope="module")
def speech():
    """The whisper.cpp sample: 11 s of President Kennedy at 16 kHz."""
    if not SPEECH.exists():
        pytest.skip("the whisper.cpp submodule is not checked out")
    with wave.open(str(SPEECH)) as file:
        assert file.getframerate() == 16000 and file.getnchannels() == 1 and file.getsampwidth() == 2
        return np.frombuffer(file.readframes(file.getnframes()), dtype="<i2").astype(np.float32) / 32768


@pytest.fixture(scope="module")
def transcriber(model_path):
    with listen.Transcriber(model_path) as transcriber:
        yield transcriber.prepare()


def silence(seconds):
    return np.zeros(int(seconds * 16000), dtype=np.float32)


# ---- Without a model

def test_versions_match_the_header_and_the_package():
    header = (ROOT / "include" / "speechwarp.h").read_text()
    in_header = re.search(r'#define SPEECHWARP_VERSION "(.*)"', header).group(1)
    project = (ROOT / "bindings" / "python-listen" / "pyproject.toml").read_text()
    in_project = re.search(r'^version = "(.*)"', project, re.M).group(1)
    assert listen.native_version() == in_header == in_project
    assert re.match(r"\d+\.\d+\.\d+", listen.engine_version())
    assert listen.system_info()


def test_catalogue_describes_every_model():
    models = listen.models()
    assert len(models) >= 10
    assert len({model.id for model in models}) == len(models)
    for model in models:
        assert model.engine == "whisper" and model.id.startswith("whisper-")
        assert model.size_bytes > 10_000_000
        assert re.fullmatch("[0-9a-f]{64}", model.sha256)
        assert model.download_url.startswith("https://") and model.download_url.endswith("/" + model.file_name)
        assert model.relative_speed >= 1
    assert listen.find_model("whisper-tiny.en").languages == ["en"]
    assert listen.find_model("whisper-tiny").languages == []
    assert listen.find_model("whisper-enormous") is None
    assert listen.model_for_file("/models/ggml-base.en.bin").id == "whisper-base.en"
    other = listen.model_for_file("/models/mine.bin")
    assert other.id == "whisper-file:mine.bin" and other.download_url is None and math.isnan(other.relative_speed)


def test_reports_a_missing_or_broken_model_file(tmp_path):
    missing = listen.Transcriber(tmp_path / "no-such-model.bin")
    with pytest.raises(FileNotFoundError):
        missing.prepare()
    assert not missing.is_ready and missing.is_multilingual is None
    junk = tmp_path / "junk.bin"
    junk.write_text("not a model")
    with pytest.raises(listen.ListenError):
        listen.Transcriber(junk).prepare()


def test_checks_its_arguments():
    with pytest.raises(ValueError):
        listen.Transcriber("x.bin").transcribe(np.zeros(10), 1000)
    with pytest.raises(ValueError):
        listen.Transcriber("x.bin").transcribe(np.zeros((10, 2)), 16000)
    with pytest.raises(ValueError):
        listen.Transcriber("x.bin").transcribe(np.zeros(10), 16000, preset="slow")


# ---- With a model

def test_transcribes_a_sentence_with_word_times(transcriber, speech):
    assert transcriber.is_ready and transcriber.is_multilingual is not None
    transcript = transcriber.transcribe(speech, 16000, language="en")
    assert "ask not what your country can do for you" in plain(transcript.text)
    previous = 0.0
    for segment in transcript.segments:
        assert previous - 0.01 <= segment.start <= segment.end <= 11.5
        assert segment.words
        for word in segment.words:
            assert segment.start - 0.01 <= word.start <= word.end <= segment.end + 0.01
            assert 0 <= word.confidence <= 1
        previous = segment.start
    no_words = transcriber.transcribe(speech.tolist(), 16000, word_timestamps=False)
    assert all(not segment.words for segment in no_words.segments)
    assert transcriber.transcribe(silence(3), 16000).segments == []
    assert transcriber.transcribe([], 16000).segments == []


def test_transcribes_at_another_rate(transcriber, speech):
    at_48k = np.interp(np.arange(len(speech) * 3) / 3, np.arange(len(speech)), speech).astype(np.float32)
    transcript = transcriber.transcribe(at_48k, 48000)
    assert "what your country can do" in plain(transcript.text)
    assert transcript.segments[-1].end <= 11.5


def test_refuses_an_unknown_language(transcriber):
    with pytest.raises(ValueError):
        transcriber.transcribe(silence(1), 16000, language="qq")


def test_cancels_a_transcription_from_another_thread(transcriber, speech):
    token = listen.CancelToken()
    token.cancel()
    with pytest.raises(listen.Cancelled):
        transcriber.transcribe(speech, 16000, cancel=token)

    four_minutes = np.concatenate([np.concatenate([speech, silence(1)])] * 20)
    token = listen.CancelToken()
    threading.Timer(0.2, token.cancel).start()
    started = time.monotonic()
    with pytest.raises(listen.Cancelled):
        transcriber.transcribe(four_minutes, 16000, cancel=token)
    assert time.monotonic() - started < 10
    assert "your country" in plain(transcriber.transcribe(speech, 16000).text)


def test_a_session_written_and_processed_on_two_threads(transcriber, speech):
    # About 75 s: the sentence three times, apart, so that the session cuts at least two chunks.
    audio = np.concatenate([speech, silence(14)] * 3)
    found = []
    with transcriber.start_session(16000, language="en") as session:
        def writer():
            for start in range(0, len(audio), 1600):
                session.write(audio[start:start + 1600])

        thread = threading.Thread(target=writer)
        thread.start()
        while thread.is_alive() or session.seconds_written - session.seconds_recognised >= 30:
            found += session.process()
            time.sleep(0.01)
        thread.join()
        assert found, "no chunk was finished before finish()"
        assert session.seconds_recognised > 0
        found += session.finish().segments
        assert session.seconds_recognised == pytest.approx(session.seconds_written, abs=0.01)
        assert session.seconds_written == pytest.approx(len(audio) / 16000)
        assert session.take() == []
        with pytest.raises(listen.ListenError):
            session.write(silence(1))
    text = " ".join(segment.text for segment in found)
    # Each time is recognised, at least in part: with the text before as context, whisper's smallest models tend to
    # shorten a sentence repeated word for word.
    assert plain(text).count("do for your country") >= 3, text
    assert all(b.start >= a.start - 0.01 for a, b in zip(found, found[1:]))
    assert found[0].start < 1 and 55 < found[-1].end <= len(audio) / 16000 + 0.01


def test_a_partial_guess(transcriber, speech):
    with transcriber.start_session(16000, preset="fast") as session:
        assert session.partial() is None
        session.write(speech)
        guess = session.partial()
        assert guess is not None and "your country" in plain(guess.text)
        assert session.take() == []
        assert "ask not what your country can do for you" in plain(session.finish().text)


def test_finishing_can_be_cancelled_and_carried_on(transcriber, speech):
    with transcriber.start_session(16000) as session:
        session.write(np.concatenate([speech, silence(1)] * 8))
        threading.Timer(0.1, session.cancel).start()
        with pytest.raises(listen.Cancelled):
            session.finish()
        text = " ".join(s.text for s in session.take() + session.finish().segments)
        assert plain(text).count("do for your country") >= 8, text


def test_a_closed_transcriber_keeps_its_model_for_open_sessions(model_path, speech):
    transcriber = listen.Transcriber(model_path)
    session = transcriber.start_session(16000)
    transcriber.close()
    session.write(speech)
    assert "your country" in plain(session.finish().text)
    session.close()
    assert not transcriber.is_ready
