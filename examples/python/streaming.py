"""Audio that arrives in pieces: write each piece, read what is ready, and ask where you are.

    python streaming.py

This is the pattern for a player or a live pipeline. It uses a generated signal so it runs with no files.
"""
import numpy as np

import speechwarp

SAMPLE_RATE = 22050

# Thirty seconds of a warbling tone in bursts, standing in for speech.
t = np.arange(30 * SAMPLE_RATE) / SAMPLE_RATE
recording = (0.3 * np.sin(2 * np.pi * 150 * t + 3 * np.sin(2 * np.pi * 2 * t)) * (t % 0.4 < 0.3)).astype(np.float32)

stream = speechwarp.Stream(SAMPLE_RATE, channels=1, speed=3)
heard = 0  # frames of output so far

for start in range(0, len(recording), 4096):
    stream.write(recording[start:start + 4096])

    # read() returns whatever is ready, which may be nothing: output lags input by a short look-ahead.
    out = stream.read()
    heard += len(out)

    if start % (40 * 4096) == 0:
        # position is the frame of the input that the next output frame comes from. Multiplying the output
        # time by the speed would be wrong, because the speed varies from moment to moment.
        print(f"heard {heard / SAMPLE_RATE:5.2f} s, which is {stream.position / SAMPLE_RATE:5.2f} s into the recording")

    if start == 80 * 4096:
        stream.speed = 6  # takes effect straight away, without a gap
        print("-- speed 6x")

stream.flush()  # the input has ended: let out what was held back
heard += len(stream.read())
print(f"done: {len(recording) / SAMPLE_RATE:.1f} s played in {heard / SAMPLE_RATE:.1f} s; "
      f"position {stream.position} of {len(recording)} frames")
