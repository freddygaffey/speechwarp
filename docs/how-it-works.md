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

## Limits worth knowing

- It is for speech. Music goes through, but the result is what you would expect from removing pitch periods
  from music.
- Speedy's constants were tuned by its authors on English read speech. Other languages and very different
  voices work but have not been measured.
- The speed correction in this library has so far been tuned on a synthetic signal, not on recordings of
  speech. The two constants are `CORRECTION_TIME` and `TYPICAL_SHORTFALL` in `src/speechwarp.c`.
