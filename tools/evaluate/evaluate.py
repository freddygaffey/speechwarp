"""Measures the options for very high speeds on synthetic speech. macOS only: it uses `say`.

    cmake -B build && cmake --build build      # from the top of the repository
    python3 tools/evaluate/evaluate.py         # needs numpy; writes into tools/evaluate/work/

Prints the syllable counter's reading of each voice, and for each speed and setting the overall speed
achieved and the syllables a second heard, overall and while speech is playing. docs/how-it-works.md has the
results of a run.
"""
import json
import os
import re
import subprocess
import wave
from pathlib import Path

import numpy as np

from syl import count

HERE = Path(__file__).resolve().parent
TOOL = os.environ.get("SPEECHWARP", str(HERE / "../../build/speechwarp"))
WORK = HERE / "work"
VOICES = ["Samantha", "Daniel", "Karen", "Moira", "Tessa"]
SPEEDS = [5, 6.5, 8]
SETTINGS = {
    "even": ["--linear"],
    "baseline": [],
    "pause-cap": ["--pause-cap", "0.06"],
    "pause-cap+floor": ["--pause-cap", "0.06", "--floor", "0.5"],
    "pause-cap+floor+rhythm": ["--pause-cap", "0.06", "--floor", "0.5", "--rhythm-gap", "0.04", "--rhythm-rate", "6"],
}
TRUE = count((HERE / "passage.txt").read_text())

def load(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768
        return x, w.getframerate()

def speech_seconds(x, rate):
    block = rate // 100
    n = len(x) // block
    db = 10 * np.log10((x[: n * block].reshape(n, block) ** 2).mean(axis=1) + 1e-12)
    return (db > db.max() - 30).sum() / 100

def make_speech(voice):
    """The passage read by `voice` at 160 words a minute, with a longer pause between paragraphs."""
    path = WORK / f"{voice}.wav"
    if not path.exists():
        paragraphs = (HERE / "passage.txt").read_text().strip().split("\n\n")
        text = WORK / "say.txt"
        text.write_text(" [[slnc 700]] ".join(paragraphs))
        aiff = WORK / f"{voice}.aiff"
        subprocess.run(["say", "-v", voice, "-r", "160", "-f", str(text), "-o", str(aiff)], check=True)
        subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@44100", "-c", "1", str(aiff), str(path)], check=True)
        aiff.unlink()
    return path


(WORK / "out").mkdir(parents=True, exist_ok=True)
rows = []
for voice in VOICES:
    src = str(make_speech(voice))
    x, rate = load(src)
    for speed in SPEEDS:
        for name, opts in SETTINGS.items():
            out = str(WORK / "out" / f"{voice}-{speed}-{name}.wav")
            r = subprocess.run([TOOL, "-v", "--speed", str(speed), *opts, src, out], capture_output=True, text=True, check=True)
            measured = float(re.search(r"([\d.]+) syllables a second in", r.stderr).group(1))
            y, _ = load(out)
            rows.append(dict(voice=voice, speed=speed, setting=name, achieved=len(x) / len(y),
                             heard=TRUE / (len(y) / rate), in_speech=TRUE / speech_seconds(y, rate),
                             counted_in=measured, true_in=TRUE / (len(x) / rate)))
(WORK / "results.json").write_text(json.dumps(rows, indent=1))

print(f"Reference: {TRUE} syllables in the passage (rule-based count)")
print("\nSyllable counter on the input (last 60 s of each file):")
for voice in VOICES:
    r = next(r for r in rows if r["voice"] == voice)
    print(f"  {voice:9s} counted {r['counted_in']:.2f}/s, reference {r['true_in']:.2f}/s ({100*(r['counted_in']/r['true_in']-1):+.0f}%)")
print("\nMean over the five voices: overall speed achieved, syllables a second heard overall, and while speech is playing")
print(f"{'speed':>6} {'setting':24s} {'achieved':>9} {'heard':>7} {'in speech':>10}")
for speed in SPEEDS:
    for name in SETTINGS:
        sel = [r for r in rows if r["speed"] == speed and r["setting"] == name]
        m = lambda k: sum(r[k] for r in sel) / len(sel)
        print(f"{speed:>6} {name:24s} {m('achieved'):9.2f} {m('heard'):7.1f} {m('in_speech'):10.1f}")
