# How it works

## The idea

Speeding speech up evenly squeezes everything by the same factor. At 2x that is fine. By 4x the consonants,
which are short to begin with and carry most of what distinguishes one word from another, are down to a few
milliseconds and start to vanish.

People who talk fast do something else. They shorten pauses most, steady vowels next, and consonants and the
transitions between sounds least. The **MACH1** algorithm (Covell, Withgott and Slaney, *MACH1: nonuniform
time-scale modification of speech*, ICASSP 1998) imitates that, and Google's
[Speedy](https://github.com/google/speedy) is an open implementation of it. speechwarp packages Speedy.

Google evaluated Speedy at 3.5x. Nothing is published for higher speeds; the author of this library
preferred it to even speed-up in a small blind test of his own between 3x and 6.5x, which is a preference and
not proof. [`examples/blind-ab-test`](../examples/blind-ab-test/)
lets you run the comparison on yourself.

## The pipeline

```
input ─► 10 ms blocks ─┬─► Speedy: how "tense" is this block? ─► a speed for the block ─┐
                       │                                                                 ▼
                       └────────── held for 0.12 s of look-ahead ──────────────► Sonic ─► output
```

**Sonic** ([waywardgeek/sonic](https://github.com/waywardgeek/sonic)) does the actual time compression. It
finds the pitch period of the voice and removes whole periods, overlapping and adding the joins, so the
pitch does not rise as it would if the audio were simply played faster. It is happy to be given a different
speed for every piece of input.

**Speedy** decides those speeds. For each 10 ms block it computes a number called *tension* from a
spectrogram of the speech:

- **Energy.** Loud relative to the last second means emphasised; quiet means a pause. The measure is spread
  80 ms into the past and 120 ms into the future, so that the quiet instant before a stop consonant is not
  mistaken for a pause. That spread into the future is why the library has to look ahead.
- **Spectral change.** How much the shape of the spectrum differs from the block before. Steady vowels change
  little; consonants and transitions change a lot.

High tension means *keep this*. For a requested speed *R* above 1, the speed of a block is

    speed = max(1, R - (R - 1) × tension)

so a tense block is slowed towards normal speed, and a slack one, such as silence, goes faster than *R*.

Only the choice of speed uses a mono mix. Sonic processes all the channels together with that one speed, so
stereo stays aligned.

## What this library adds

The files in `third_party/` are upstream's, unmodified. Upstream joins Speedy to Sonic with a shim,
`soniclib.c`, written for a command-line tool. `src/speechwarp.c` replaces the shim. It gives Speedy and Sonic
exactly the data the shim does, which `tests/test_parity.c` checks sample for sample, and differs in these
ways.

**The speed is the speed you get.** Tension is more often positive than negative in speech, so Speedy's own
speeds average well below the one requested: about 15% below on audiobooks. Upstream corrects by adding 0.1
to the speed for every second the output has run over. At 10x a shortfall of 1.5 needs fifteen seconds of
overrun before the correction is big enough, which takes minutes. speechwarp scales that gain with the square
of the speed so that it settles in about four seconds of input at any speed, and starts from the typical
shortfall instead of from zero, so the opening seconds are not slow. Like upstream it only ever speeds up.

**Position.** Each time a block goes to Sonic, the stream notes how much input has gone in and how much
output exists. `speechwarp_position` interpolates in those notes at the point you have read up to. Upstream
has nothing like it.

**Nothing is lost or printed.** Upstream's flush drops the last partial block, up to 10 ms; its shim cannot be
written to again after a flush; it prints two lines to standard output when it starts; and it does not check
whether its allocations succeeded. None of that is true here.

**A fast transform at 44.1 kHz.** Speedy takes a Fourier transform of 30 ms of audio per block. At 44.1 kHz
that is 1322 points, and 1322 = 2 × 661 with 661 prime. KISS FFT, the transform upstream can be built with
when FFTW is not wanted, does prime factors the slow way, and the analysis ran at about 11 times real time.
`src/fft.c` uses Bluestein's algorithm for such sizes, which turns a transform of any length into two
power-of-two transforms, and the same analysis now runs about 250 times real time. At rates whose size KISS
FFT handles well (48 kHz, 22.05 kHz, 16 kHz) it is called directly and the results are bit-for-bit
upstream's.

**One set of names.** Sonic and KISS FFT are embedded in many programs, and Android ships a Sonic of its own.
`src/rename.h` gives every third-party symbol a `speechwarp_priv_` prefix, so the library can be linked next
to another copy, statically too.

## Options for very high speeds

Audiobook narration runs at 4 to 5 syllables a second. At 6x that would be 24 to 30, and the best trained
listeners are known to follow 17 to 22 ([the research notes](research-high-speed.md) have the sources). Five
options, all off by default, are meant for 5x to 8x. With them off, the library does exactly what it did
before, and `tests/test_parity.c` still checks that sample for sample.

```
input ─► syllable counter ─► pause cap ─► Speedy and Sonic (with speed floor) ─► rhythm ─► output
                                   └──────── keep overall speed ────────┘
```

**Pause cap** (`speechwarp_set_pause_cap`, seconds). Before Speedy sees the audio, every pause is shortened
to at most this much input. A 10 ms block is part of a pause if it is 30 dB or more below the level of recent
speech (the loudest recent block, forgotten at 0.5 dB a second), or below -70 dBFS. The first half of a pause
goes on as it is; the rest is held back, and only the last half-cap's worth is kept when speech returns. The
cut is crossfaded over one block so it does not click, and `speechwarp_position` accounts for every frame left
out. 0.06 s keeps a pause just long enough to hear that there was one.

**Keep overall speed** (`speechwarp_set_keep_speed`, on by default, but it only matters while the pause cap
or rhythm is on). Time saved by the pause cap is counted against the average-speed correction, and time spent
in rhythm gaps is counted for it, so 6x still takes a sixth of the time and the saving goes to slower words.
For that the correction is allowed to slow down as well as speed up, but never below 1x, and it never more
than doubles the speed. With it off, trimmed pauses make playback faster than the speed set.

**Speed floor** (`speechwarp_set_speed_floor`, a fraction of the speed). Speedy can slow the tensest blocks
all the way to 1x, so at 8x the slack ones must run far faster than 8x to make up the time. A floor of 0.5
keeps every block at 4x or more at 8x. It is applied in `src/speechwarp.c` to the speed Speedy chooses; the
upstream files are untouched. 1 makes it the same as linear.

**Rhythm** (`speechwarp_set_rhythm_gap`, seconds, and `speechwarp_set_rhythm_rate`, gaps a second). Ghitza
and Greenberg (2009) found that speech compressed to a third of its length was far easier to follow with
short silences put back at a regular rate: the gaps let the listener's syllable rhythm keep up. After Sonic,
a small stage puts a silence of the gap length into the output at the rate set, at the quietest point within
30% of a chunk of where the rate puts it, with 5 ms raised-cosine fades either side. The gaps count as output
for keep overall speed, so the speech between them is compressed harder; `speechwarp_position` holds still
during a gap. The stage adds about 0.1 s of latency. It takes Sonic's output as each block is released, not
as it is read, so the output still does not depend on how reads and writes are timed.

**Syllable rate** (`speechwarp_syllable_rate`). An estimate of syllables a second in the input over the last
60 s, from syllable nuclei: peaks of loudness in voiced sound, each with a dip on both sides (de Jong and
Wempe, 2009). 10 ms frames band-passed to 250 Hz to 3 kHz, smoothed over 4 frames; a peak must stand 1 dB
above the dips, be within 25 dB of the loudest recent speech, be voiced (under 3000 zero crossings a second),
and be at least 60 ms after the last. It runs on all input whatever the options, and costs little. It was
ported from the C# player this library was written for.

### Rules that follow the speed

A fixed pause cap and a fixed floor do different things at different speeds, and a player that ramps the
speed up during a session would have to keep recomputing them. Two rules do that inside the library, applied
whenever the speed changes:

**Heard pause** (`speechwarp_set_heard_pause`, seconds of output, and a speed from which it applies). The pause
cap in force is the heard length times the speed, clamped to 0.03 to 0.4 s of input; below the "from" speed
pauses are left alone. The reason is that the pause cap is measured in input. A fixed cap is heard as roughly
the cap divided by the speed, so it vanishes as the speed rises. Measured on a synthetic signal (0.7 s voiced
bursts with 300 ms of silence between them, Speedy, keep overall speed on; median of 39 pauses):

| Speed | No cap | Fixed cap 0.06 s | Heard pause 0.03 s (cap in force) |
|-------|--------|------------------|-----------------------------------|
| 2x | 116 ms | 36 ms | 36 ms (0.06 s) |
| 3x | 72 ms | 20 ms | 28 ms (0.09 s) |
| 5x | 40 ms | 12 ms | 23 ms (0.15 s) |
| 7.5x | 24 ms | 8 ms | 21 ms (0.225 s) |

From 2x to 7.5x, 3.75 times faster, the fixed cap's pause shrank 4.7 times, about the speed to the power 1.2:
the cap over the speed, plus a few milliseconds of crossfade. (An earlier note said heard pauses shrink with
the square of the speed; this measurement does not bear that out.) The heard-pause rule holds them at 21 to
28 ms instead. Speedy itself hurries pauses by a roughly constant factor, 0.6 to 0.77 of the even
compression, which is why the measured length runs a little under the cap divided by the speed.

**Floor blend** (`speechwarp_set_floor_blend`, a fraction and two speeds). The floor in force is 0 below the
first speed and rises linearly to the fraction at the second, so a ramp in 0.1x steps changes the sound in
steps too small to hear rather than all at once at some speed.

Each rule and its fixed option replace each other, and `speechwarp_get_pause_cap` and
`speechwarp_get_speed_floor` report the value in force.

### A first evaluation

Five macOS voices (Samantha, Daniel, Karen, Moira and Tessa) read the same 300-word passage at 160 words a
minute, 88 to 101 s each and about 15% pauses. Averages over the five:

| Speed | Setting | Overall speed | Syllables a second, overall | While speech plays |
|-------|---------|---------------|-----------------------------|--------------------|
| 5x | even | 5.03 | 21.0 | 24.0 |
| 5x | Speedy | 5.04 | 21.0 | 22.9 |
| 5x | + pause cap 0.06 s | 5.05 | 21.0 | 21.5 |
| 5x | + floor 0.5 | 5.06 | 21.1 | 21.6 |
| 5x | + rhythm 40 ms, 6 a second | 5.00 | 20.8 | 27.6 |
| 6.5x | even | 6.55 | 27.3 | 30.9 |
| 6.5x | Speedy | 6.56 | 27.4 | 29.5 |
| 6.5x | + pause cap 0.06 s | 6.57 | 27.4 | 27.7 |
| 6.5x | + floor 0.5 | 6.59 | 27.5 | 27.9 |
| 6.5x | + rhythm 40 ms, 6 a second | 6.52 | 27.2 | 35.7 |
| 8x | even | 8.09 | 33.7 | 37.8 |
| 8x | Speedy | 8.10 | 33.8 | 36.0 |
| 8x | + pause cap 0.06 s | 8.11 | 33.8 | 34.0 |
| 8x | + floor 0.5 | 8.13 | 33.9 | 34.1 |
| 8x | + rhythm 40 ms, 6 a second | 8.05 | 33.6 | 43.6 |

"While speech plays" leaves out output quieter than 30 dB below the loudest, which is the pauses and gaps;
the syllables are a rule-based count of the text (386). What it shows:

- Every setting lands on the speed asked for, to within 2%.
- Speedy already squeezes pauses hard, so the pause cap gains less on top of it than on even speed-up: the
  words come about 6% slower (29.5 to 27.7 syllables a second at 6.5x), which is nearly all there is to gain,
  since the overall rate is 27.4. On narration with longer pauses than these voices leave, it gains more.
- The floor leaves the averages alone and evens out the effort: at 8x the slowest 5% of the speech went from
  3.2x to 4.3x.
- Rhythm buys its gaps by playing the words a third faster. Whether the gaps make up for that is the open
  question; it is the one to settle by listening.
- The syllable counter read the input within -10% to +3% of the count from the text.

[`tools/evaluate/evaluate.py`](../tools/evaluate/evaluate.py) reproduces the table on a Mac. Whether any of this
helps comprehension needs listening, for example with the
[blind A/B test](../examples/blind-ab-test/), which can compare any two of these settings.

## Numbers

| | |
|---|---|
| Block | 10 ms |
| Analysis window | 15 ms, zero-padded to 30 ms for the transform |
| Look-ahead, nonlinear | about 0.15 s of input (0.12 s for Speedy, the rest Sonic's) |
| Look-ahead, even | about 0.04 s of input |
| Speed range | 0.05 to 20 |
| Settling time of the average speed | about 4 s of input |
| Internal sample format | 16-bit |
| Pause cap | 0, or 0.01 to 1 s; a pause is 30 dB below recent speech |
| Speed floor | 0 to 1 of the speed |
| Rhythm gap | 0, or 0.005 to 0.2 s, faded over 5 ms; rate 1 to 16 a second, default 5 |
| Extra latency with rhythm on | about 0.1 s of output |
| Syllable rate | over the last 60 s; none until 10 s |

## Limits worth knowing

- It is for speech. Music goes through, but the result is what you would expect from removing pitch periods
  from music.
- Speedy's constants were tuned by its authors on English read speech. Other languages and very different
  voices work but have not been measured.
- The speed correction was tuned on a synthetic signal and checked on synthetic speech from macOS voices (see
  above), not on recordings of people. The two constants are `CORRECTION_TIME` and `TYPICAL_SHORTFALL` in
  `src/speechwarp.c`.
- The options for very high speeds have been measured but not yet listened to systematically. The pause
  threshold assumes a quiet background; a noisy recording may not reach 30 dB below its speech.
