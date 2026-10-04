# Blind A/B listening test

A small Flask app for finding out which of two speed-up settings *you* follow better: by default even
speed-up (plain Sonic) against Speedy's nonlinear speed-up, or any two combinations of the options for very
high speeds. It was written to make those decisions for an audiobook player, and it is rough.

A long speech recording plays as one continuous stream. **A** and **B** are the two settings in hidden, shuffled
order; switching carries on from the same place in the recording rather than replaying it. When you have
decided, vote, and it moves to a different recording and reshuffles. Nothing on the page shows a time, a file
name or which method is which.

Both versions are rendered to the same length, so the speed slider is the true overall speed and duration
gives nothing away. No setting lands exactly on the speed asked for, so the app adjusts each request until the
result is the length wanted.

## No audio is included

Bring your own. Put at least two **16-bit PCM WAV** files of speech in `src/`. Several minutes each works
best: at 8x, ten minutes of source is 75 seconds of listening.

`src/`, the rendered `cache/` and your `votes.jsonl` are git-ignored.

## Running it

The app shells out to the `speechwarp` command-line tool. Build it from the top of this repository:

```sh
cmake -B build && cmake --build build
```

Then, in this folder:

```sh
pip install flask
python app.py          # http://127.0.0.1:5005/
```

Set `SPEECHWARP` to use a `speechwarp` binary somewhere else.

### Choosing what to compare

Name two settings on the command line:

```sh
python app.py speedy pause-cap
python app.py pause-cap pause-cap-floor-rhythm
```

| Setting | speechwarp options |
|---------|--------------------|
| `even` | `--linear` |
| `speedy` | (none: nonlinear speed-up) |
| `pause-cap` | `--pause-cap 0.06` |
| `pause-cap-floor` | `--pause-cap 0.06 --floor 0.5` |
| `pause-cap-floor-rhythm` | `--pause-cap 0.06 --floor 0.5 --rhythm-gap 0.04 --rhythm-rate 6` |

Or define your own from any of the tool's options (`speechwarp --help`):

```sh
python app.py --method short="--pause-cap 0.03" --method long="--pause-cap 0.15" short long
```

Votes record which pair was compared, and `/results` shows the votes for the pair the app is running with.

## Using it

| Control | What it does |
|---------|--------------|
| **A** / **B**, or the A and B keys | Switch version, continuing from the same place |
| Space | Flip to the other version |
| P, or **Pause** | Pause and resume |
| Speed slider | True overall speed, 2x to 10x. Applies straight away and keeps your place |
| **A is better** / **B is better** / **No difference** | Record a vote and move to another recording |

`/results` shows which setting you preferred at each speed. Opening it ends the blind test, so rate a good
number first.

## Known rough edges

- Tested in Chrome only.
- Each version is decoded fully into memory, which is fine for minutes of audio and not for hours.
- A new speed takes a second or two to render the first time.
- Votes are appended to a local file; there is no notion of users or sessions.
