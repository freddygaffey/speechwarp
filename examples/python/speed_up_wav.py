"""Speed up a WAV file.

    python speed_up_wav.py talk.wav talk-3x.wav --speed 3

Reads 16-bit PCM WAV with the standard library, so it needs nothing but speechwarp and NumPy. For other
formats, read the file with a library such as soundfile and pass the array to speechwarp.speed_up the same way.
"""
import argparse
import wave

import numpy as np

import speechwarp

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("input")
parser.add_argument("output")
parser.add_argument("--speed", type=float, default=2.0)
parser.add_argument("--linear", action="store_true", help="speed everything up evenly, for comparison")
args = parser.parse_args()

with wave.open(args.input) as source:
    if source.getsampwidth() != 2:
        raise SystemExit("this example reads 16-bit WAV only")
    sample_rate = source.getframerate()
    channels = source.getnchannels()
    samples = np.frombuffer(source.readframes(source.getnframes()), dtype=np.int16)

# Mono is a one-dimensional array; anything else is (frames, channels).
if channels > 1:
    samples = samples.reshape(-1, channels)

fast = speechwarp.speed_up(samples, sample_rate, args.speed, nonlinear=0 if args.linear else 1)

with wave.open(args.output, "wb") as out:
    out.setnchannels(channels)
    out.setsampwidth(2)
    out.setframerate(sample_rate)
    out.writeframes(fast.tobytes())

print(f"{len(samples) / sample_rate:.1f} s in, {len(fast) / sample_rate:.1f} s out, "
      f"{len(samples) / len(fast):.2f}x")
