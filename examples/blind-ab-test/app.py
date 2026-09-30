"""Blind A/B listening test: even speed-up vs Speedy (nonlinear). See README.md in this folder.

One book excerpt plays as a continuous stream. A and B are the two methods, in hidden random order; toggling
switches method mid-stream and carries on from the same place in the book. Both are rendered to the same
length, so equal playback time means equal book position. The speed slider is the true overall speed.
"""
import json
import os
import random
import secrets
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
# Google's speedy_wave command-line tool does the rendering for now; see README.md for how to build it.
SPEEDY = Path(os.environ.get("SPEEDY_WAVE", HERE / "speedy_wave"))

app = Flask(__name__)
clips = {}     # clip token -> wav path
sessions = {}  # session token -> {"source", "a": method, "b": method}


def duration(path):
    with wave.open(str(path)) as w:
        return w.getnframes() / w.getframerate()


def run_speedy(src, out, speed, nonlinear):
    args = [str(SPEEDY), f"--speed={speed:.4f}", "--input", str(src), "--output", str(out)]
    args.insert(2, "--nonlinear=1.0" if nonlinear else "--linear")
    subprocess.run(args, check=True, capture_output=True)


def render(src, speed):
    """Both methods at a true overall speed of `speed`, equal in length. Returns {"even": path, "speedy": path}."""
    speedy_out = CACHE / f"{src.stem}-{speed:g}-speedy.wav"
    even_out = CACHE / f"{src.stem}-{speed:g}-even.wav"
    if not (speedy_out.exists() and even_out.exists()):
        # Speedy slows down for consonants, so it overshoots the requested length. Ask for more until the
        # result is the length we want.
        nominal = speed / 0.85
        for _ in range(3):
            run_speedy(src, speedy_out, nominal, nonlinear=True)
            actual = duration(src) / duration(speedy_out)
            if abs(actual - speed) / speed < 0.01:
                break
            nominal *= speed / actual
        run_speedy(src, even_out, duration(src) / duration(speedy_out), nonlinear=False)
    return {"even": even_out, "speedy": speedy_out}


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
    methods = random.sample(["even", "speedy"], 2)
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
                            "time": time.time()}) + "\n")
    return jsonify(votes=len(load_votes()))


@app.get("/results")
def results():
    table = defaultdict(lambda: defaultdict(int))
    for v in load_votes():
        table[v["speed"]][v["preferred"]] += 1
    rows = "".join(
        f"<tr><td>{speed:g}x</td><td>{c['even']}</td><td>{c['speedy']}</td><td>{c['same']}</td></tr>"
        for speed, c in sorted(table.items()))
    return f"""<!doctype html><title>Results</title>
<style>body{{font:16px system-ui;margin:40px;background:#1d1a2e;color:#efe7d6}}td,th{{padding:8px 18px;text-align:left}}
table{{border-collapse:collapse;margin-bottom:24px}}tr{{border-bottom:1px solid #444}}a{{color:#f0b25a}}</style>
<h2>Which method you preferred</h2>
<table><tr><th>Speed</th><th>Even speed-up</th><th>Speedy</th><th>No difference</th></tr>{rows}</table>
<p>{len(load_votes())} votes. Speed is the true overall speed of both versions.</p>
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
    if len(SRC) < 2:
        raise SystemExit(f"Put at least two mono 16-bit WAV files of speech in {HERE / 'src'} (see README.md).")
    if not SPEEDY.exists():
        raise SystemExit(f"speedy_wave not found at {SPEEDY}. Build it (see README.md) or set SPEEDY_WAVE.")
    app.run(port=5005, threaded=True)
