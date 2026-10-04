"""Blind A/B listening test between two speed-up settings. See README.md in this folder.

One book excerpt plays as a continuous stream. A and B are the two settings, in hidden random order; toggling
switches mid-stream and carries on from the same place in the book. Both are rendered to the same length, so
equal playback time means equal book position. The speed slider is the true overall speed.

    python app.py                       # even speed-up against Speedy
    python app.py speedy pause-cap      # any two of the settings in METHODS
    python app.py --method mine="--pause-cap 0.1 --floor 0.3" speedy mine
"""
import argparse
import hashlib
import json
import os
import random
import secrets
import shlex
import subprocess
import time
import wave
from collections import defaultdict
from pathlib import Path

from flask import Flask, abort, jsonify, request, send_file

HERE = Path(__file__).parent
SRC = sorted((HERE / "src").glob("*.wav"))  # your own speech recordings; none are shipped
CACHE = HERE / "cache"
CACHE.mkdir(exist_ok=True)
VOTES = HERE / "votes.jsonl"
# The speechwarp command-line tool does the rendering; see README.md for how to build it.
SPEECHWARP = Path(os.environ.get("SPEECHWARP", HERE / "../../build/speechwarp"))

# Settings that can be compared: a name and the speechwarp options that make it.
METHODS = {
    "even": ["--linear"],
    "speedy": [],
    "pause-cap": ["--pause-cap", "0.06"],
    "pause-cap-floor": ["--pause-cap", "0.06", "--floor", "0.5"],
    "pause-cap-floor-rhythm": ["--pause-cap", "0.06", "--floor", "0.5", "--rhythm-gap", "0.04",
                               "--rhythm-rate", "6"],
}
COMPARED = ["even", "speedy"]  # replaced from the command line

app = Flask(__name__)
clips = {}     # clip token -> wav path
sessions = {}  # session token -> {"source", "a": method, "b": method}


def duration(path):
    with wave.open(str(path)) as w:
        return w.getnframes() / w.getframerate()


def run_speechwarp(src, out, speed, method):
    args = [str(SPEECHWARP), "--speed", f"{speed:.4f}", *METHODS[method], str(src), str(out)]
    subprocess.run(args, check=True, capture_output=True)


def render_to(src, out, method, seconds):
    """Render `src` with `method`, adjusting the speed asked for until the result lasts `seconds`."""
    nominal = duration(src) / seconds
    for _ in range(5):
        run_speechwarp(src, out, nominal, method)
        if abs(duration(out) - seconds) / seconds < 0.005:
            break
        nominal = min(20, nominal * duration(out) / seconds)


def render(src, speed):
    """Both compared settings at a true overall speed of `speed`, equal in length. Returns {method: path}."""
    def name(method):  # the options are in the name, so redefining a setting renders it again
        options = hashlib.sha1(" ".join(METHODS[method]).encode()).hexdigest()[:8]
        return CACHE / f"{src.stem}-{speed:g}-{method}-{options}.wav"
    paths = {m: name(m) for m in COMPARED}
    # No setting lands exactly on the speed asked for, and equal length is what keeps the test blind.
    for method, path in paths.items():
        if not path.exists():
            render_to(src, path, method, duration(src) / speed)
    return paths


def load_votes():
    if not VOTES.exists():
        return []
    return [json.loads(line) for line in VOTES.read_text().splitlines() if line.strip()]


def clip_urls(session, speed):
    paths = render(next(s for s in SRC if s.stem == session["source"]), speed)
    urls = {}
    for label in ("a", "b"):
        token = secrets.token_urlsafe(12)
        clips[token] = paths[session[label]]
        urls[label] = f"/clip/{token}"
    return urls


@app.get("/session")
def new_session():
    """Start on a new book excerpt, with the two methods shuffled behind A and B."""
    speed = float(request.args["speed"])
    previous = request.args.get("previous")
    src = random.choice([s for s in SRC if s.stem != previous])
    methods = random.sample(COMPARED, 2)
    token = secrets.token_urlsafe(12)
    sessions[token] = {"source": src.stem, "a": methods[0], "b": methods[1]}
    return jsonify(session=token, source=src.stem, **clip_urls(sessions[token], speed))


@app.get("/respeed")
def respeed():
    """Same excerpt and same A/B assignment, at a new speed."""
    session = sessions.get(request.args["session"]) or abort(404)
    return jsonify(**clip_urls(session, float(request.args["speed"])))


@app.get("/clip/<token>")
def clip(token):
    path = clips.get(token) or abort(404)
    response = send_file(path, mimetype="audio/wav", download_name="clip.wav", conditional=True)
    response.headers["Cache-Control"] = "no-store"
    return response


@app.post("/vote")
def vote():
    data = request.get_json()
    session = sessions.pop(data["session"], None) or abort(404)
    choice = data["choice"]  # "a", "b" or "same"
    with VOTES.open("a") as f:
        f.write(json.dumps({"source": session["source"], "speed": data["speed"],
                            "preferred": session[choice] if choice in ("a", "b") else "same",
                            "compared": sorted(COMPARED),
                            "time": time.time()}) + "\n")
    return jsonify(votes=len(load_votes()))


@app.get("/results")
def results():
    table = defaultdict(lambda: defaultdict(int))
    for v in load_votes():
        if v.get("compared", ["even", "speedy"]) == sorted(COMPARED):
            table[v["speed"]][v["preferred"]] += 1
    rows = "".join(
        f"<tr><td>{speed:g}x</td>" + "".join(f"<td>{c[m]}</td>" for m in COMPARED) + f"<td>{c['same']}</td></tr>"
        for speed, c in sorted(table.items()))
    heads = "".join(f"<th>{m}</th>" for m in COMPARED)
    return f"""<!doctype html><title>Results</title>
<style>body{{font:16px system-ui;margin:40px;background:#1d1a2e;color:#efe7d6}}td,th{{padding:8px 18px;text-align:left}}
table{{border-collapse:collapse;margin-bottom:24px}}tr{{border-bottom:1px solid #444}}a{{color:#f0b25a}}</style>
<h2>Which setting you preferred</h2>
<table><tr><th>Speed</th>{heads}<th>No difference</th></tr>{rows}</table>
<p>{sum(sum(c.values()) for c in table.values())} votes between these two settings. Speed is the true overall
speed of both versions.</p>
<p><a href="/">Back to the test</a></p>"""


@app.get("/")
def index():
    return PAGE


PAGE = """<!doctype html><meta charset="utf-8"><title>Blind A/B speed test</title>
<style>
body{font:18px system-ui;margin:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:26px;background:#1d1a2e;color:#efe7d6}
button{font:inherit;border:0;border-radius:10px;cursor:pointer;font-weight:600;background:#2a2640;color:#efe7d6;padding:12px 22px}
button:disabled{opacity:.35;cursor:default}
.ab{width:150px;padding:34px 0;font-size:34px;border:3px solid transparent}
.ab.on{border-color:#f0b25a;color:#f0b25a}
.vote{background:#f0b25a;color:#1d1a2e}
.row{display:flex;gap:16px;align-items:center}
#status{height:24px;color:#9c95b0}
input[type=range]{width:340px;accent-color:#f0b25a}
a,small{color:#9c95b0;font-size:14px}
</style>
<div class="row"><label for="speed">Speed</label><input id="speed" type="range" min="2" max="10" step="0.5" value="6"><b id="speedLabel">6x</b></div>
<div id="status">Press Start</div>
<div class="row"><button class="ab" id="a" disabled>A</button><button class="ab" id="b" disabled>B</button></div>
<div class="row"><button id="start">Start</button><button id="pause" disabled>Pause</button></div>
<div class="row"><button class="vote" id="voteA" disabled>A is better</button><button class="vote" id="voteB" disabled>B is better</button><button id="voteSame" disabled>No difference</button></div>
<small>Keys: A / B switch version, Space toggles, P pause. Votes: <span id="count">0</span></small>
<a href="/results" target="_blank">Show results (spoils the blind test)</a>
<script>
const $ = id => document.getElementById(id);
// Web Audio: one context unlocked by the Start click, both versions decoded into memory. Switching is instant
// and exactly in step, nothing on the page shows a time, and the browser cannot block a later play().
let ctx = null, node = null;
const buffers = {a: null, b: null};
let session = null, source = null, current = 'a', playing = false, busy = false;
let fraction = 0, startedAt = 0;      // place in the excerpt (0-1) when playback last started, and when that was
const controls = ['a', 'b', 'pause', 'voteA', 'voteB', 'voteSame'];
const enable = on => controls.forEach(id => $(id).disabled = !on);
const status = text => $('status').textContent = text;
const speed = () => Number($('speed').value);

// Where we are in the excerpt, as a proportion: the same proportion in either version is the same place in the book.
const position = () => playing ? Math.min(1, fraction + (ctx.currentTime - startedAt) / buffers[current].duration) : fraction;

function show() {
  $('a').classList.toggle('on', current === 'a');
  $('b').classList.toggle('on', current === 'b');
  $('pause').textContent = playing ? 'Pause' : 'Play';
  status(playing ? 'Playing ' + current.toUpperCase() : 'Paused');
}

function stop() {
  if (!node) return;
  node.onended = null;
  try { node.stop(); } catch (e) {}
  node = null;
}

function start(which) {
  fraction = position();
  stop();
  current = which;
  const buffer = buffers[which];
  node = ctx.createBufferSource();
  node.buffer = buffer;
  node.connect(ctx.destination);
  node.onended = () => { fraction = 1; playing = false; node = null; status('End of excerpt: vote to move on'); $('pause').textContent = 'Play'; };
  node.start(0, Math.min(fraction, 0.999) * buffer.duration);
  startedAt = ctx.currentTime;
  playing = true;
  show();
}

function pause() {
  fraction = position();
  stop();
  playing = false;
  show();
}

async function fetchBuffers(urls) {
  const decode = async url => ctx.decodeAudioData(await (await fetch(url)).arrayBuffer());
  [buffers.a, buffers.b] = await Promise.all([decode(urls.a), decode(urls.b)]);
}

async function newSession() {
  busy = true; enable(false); status('Loading a new book');
  try {
    const r = await (await fetch(`/session?speed=${speed()}&previous=${source || ''}`)).json();
    await fetchBuffers(r);
    session = r.session; source = r.source; fraction = 0; playing = false;
    enable(true);
    start('a');
  } catch (e) { status('Could not load: ' + e.message); }
  busy = false;
}

async function changeSpeed() {
  if (!session || busy) return;
  busy = true;
  const wasPlaying = playing;
  if (playing) pause();
  enable(false); status('Changing speed');
  try {
    await fetchBuffers(await (await fetch(`/respeed?session=${session}&speed=${speed()}`)).json());
    enable(true);
    if (wasPlaying) start(current); else show();
  } catch (e) { status('Could not change speed: ' + e.message); }
  busy = false;
}

async function vote(choice) {
  if (!session || busy) return;
  busy = true;
  if (playing) pause();
  enable(false); status('');
  const voted = session; session = null;
  const r = await (await fetch('/vote', {method: 'POST', headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({session: voted, choice, speed: speed()})})).json();
  $('count').textContent = r.votes;
  setTimeout(newSession, 1000);
}

const ready = () => session && !busy;
const switchTo = which => { if (ready()) start(which); };
const togglePause = () => { if (ready()) { if (playing) pause(); else start(current); } };

// After any click, give focus back to the page. Otherwise Space also "presses" the focused button, and keys
// typed while the slider has focus move the slider.
document.addEventListener('click', () => document.activeElement && document.activeElement.blur());

$('start').onclick = async () => {
  ctx = new (window.AudioContext || window.webkitAudioContext)();
  await ctx.resume();
  $('start').style.display = 'none';
  newSession();
};
$('a').onclick = () => switchTo('a');
$('b').onclick = () => switchTo('b');
$('pause').onclick = togglePause;
$('voteA').onclick = () => vote('a');
$('voteB').onclick = () => vote('b');
$('voteSame').onclick = () => vote('same');
$('speed').oninput = () => $('speedLabel').textContent = speed() + 'x';
$('speed').onchange = () => { $('speed').blur(); changeSpeed(); };
document.addEventListener('keydown', e => {
  if (e.metaKey || e.ctrlKey || e.altKey) return;
  const key = e.key.toLowerCase();
  if (key === ' ') { e.preventDefault(); switchTo(current === 'a' ? 'b' : 'a'); }
  else if (key === 'a') switchTo('a');
  else if (key === 'b') switchTo('b');
  else if (key === 'p') togglePause();
});
// Space is handled on keydown above; stop the browser also clicking a focused button on keyup.
document.addEventListener('keyup', e => { if (e.key === ' ') e.preventDefault(); });
</script>"""

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Blind A/B test between two speed-up settings.")
    parser.add_argument("settings", nargs="*", default=COMPARED,
                        help=f"the two settings to compare (default: even speedy). Known: {', '.join(METHODS)}")
    parser.add_argument("--method", action="append", default=[], metavar='NAME="OPTIONS"',
                        help="define a setting from speechwarp options, e.g. mine=\"--pause-cap 0.1\"")
    options = parser.parse_args()
    for definition in options.method:
        name, _, flags = definition.partition("=")
        METHODS[name] = shlex.split(flags)
    if len(options.settings) != 2 or options.settings[0] == options.settings[1] or \
            any(m not in METHODS for m in options.settings):
        raise SystemExit(f"Name two different settings from: {', '.join(METHODS)}")
    COMPARED[:] = options.settings
    if len(SRC) < 2:
        raise SystemExit(f"Put at least two 16-bit PCM WAV files of speech in {HERE / 'src'} (see README.md).")
    if not SPEECHWARP.exists():
        raise SystemExit(f"speechwarp not found at {SPEECHWARP}. Build it (see README.md) or set SPEECHWARP.")
    app.run(port=5005, threaded=True)
