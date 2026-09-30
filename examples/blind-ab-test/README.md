# Blind A/B listening test

A small Flask app for finding out which speed-up method *you* follow better: even speed-up (plain Sonic) or
Speedy's nonlinear speed-up. It was written to make that decision for an audiobook player, and it is rough.

A long speech recording plays as one continuous stream. **A** and **B** are the two methods in hidden, shuffled
order; switching carries on from the same place in the recording rather than replaying it. When you have
decided, vote, and it moves to a different recording and reshuffles. Nothing on the page shows a time, a file
name or which method is which.

Both versions are rendered to the same length, so the speed slider is the true overall speed and duration
gives nothing away. Neither method lands exactly on the speed asked for, so the app adjusts its request until
the nonlinear version is the length wanted, then renders the even version to match.

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

## Using it

| Control | What it does |
|---------|--------------|
| **A** / **B**, or the A and B keys | Switch version, continuing from the same place |
| Space | Flip to the other version |
| P, or **Pause** | Pause and resume |
| Speed slider | True overall speed, 2x to 10x. Applies straight away and keeps your place |
| **A is better** / **B is better** / **No difference** | Record a vote and move to another recording |

`/results` shows which method you preferred at each speed. Opening it ends the blind test, so rate a good
number first.

## Known rough edges

- Tested in Chrome only.
- Each version is decoded fully into memory, which is fine for minutes of audio and not for hours.
- A new speed takes a second or two to render the first time.
- Votes are appended to a local file; there is no notion of users or sessions.
