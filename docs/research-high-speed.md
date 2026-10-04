# Research: listening at 5x to 8x

Notes for deciding what speechwarp, and players built on it, should try next. Most published work stops at
3x to 4x. The target here is 5x to 8x, where much less is known, so each proposal below says how strong the
evidence is. Gathered in October 2026.

## What sets the limit

Audiobook narration runs at roughly 150 to 160 words a minute, about 4 to 5 syllables a second. Multiplying:

| Speed | Syllables a second, if speech alone is compressed |
|-------|----------------------------------------------------|
| 2x | 8 to 10 |
| 4x | 16 to 20 |
| 5x | 20 to 25 |
| 6x | 24 to 30 |
| 8x | 32 to 40 |

Untrained listeners top out around 8 syllables a second. Blind screen-reader users, after long practice,
understand synthetic speech at 17 to 22 syllables a second, about 680 to 880 words a minute (Moos and
Trouvain; Dietrich, Hertrich and Ackermann). Nobody has been shown to follow 30.

So **at 6x and above, compression alone asks for more than anyone is known to manage.** Two things can close
the gap:

1. **Spend less of the speed on speech.** Audiobooks are roughly 10 to 15% pauses. Removing them before
   compressing lets 6x overall play the words at about 5x.
2. **Make each syllable easier to hear at a given rate:** compress unevenly (MACH1), keep a rhythm the brain
   can lock on to, or use a voice that stays crisp when fast.

## How the existing algorithms compare

| Method | What it does | At 5x to 8x |
|--------|--------------|-------------|
| Even, pitch-synchronous (Sonic, PSOLA, WSOLA) | Removes whole pitch periods evenly | Consonants shrink to a few ms and vanish. Sonic is still the best plain compressor for speech: time-domain methods avoid the "phasey", metallic sound of phase vocoders. |
| MACH1 / Speedy (this library) | Compresses pauses and steady vowels most, consonants least | Covell, Withgott and Slaney: 5 to 31 points better comprehension than even compression at the same rate, no significant loss between 2.5x and 4.2x, preferred 95% of the time. Untested above 4.2x. |
| Phase vocoder family (Rubber Band R3, Signalsmith Stretch, Bungee, Elastique) | Frequency-domain stretching | Built for music. Smearing and phasiness grow with the ratio, and speech suffers first. Licences: Rubber Band is GPL or paid; Signalsmith is MIT; Bungee is MPL-2.0. |
| ESOLA (epoch-synchronous overlap-add) | Like PSOLA, aligned to glottal closures | Cleaner joins than Sonic and fast enough for real time. A possible replacement for Sonic under Speedy, but the gain at 8x is unknown. |
| Neural TSM (STSM-FiLM 2025; Neural ATSM, Interspeech 2024; DiffATSM 2025) | A network re-synthesises the speech at the new rate | Trained and tested at 0.5x to 2x. Heavy for a phone, and unproven at high rates. Not ready. |
| Pause shortening (Overcast Smart Speed and similar) | Cuts silences | Saves about 10 to 20% of the time with no change to the voice. |
| Silence insertion / "repackaging" (Ghitza and Greenberg 2009) | Compresses speech, then puts short gaps back at a regular rate | At 3x, compression alone gave about 50% word errors; gaps of 20 to 120 ms between chunks restored much of it. The gaps let the listener's syllable rhythm (theta, roughly 4 to 8 Hz) keep up. Not tested at 5x+. |
| Differential silence compression (arXiv 1901.07239) | Compresses silence twice as much as speech | Better than even compression at 3x, at almost no cost. |

## Training

- **Adaptation is fast.** Dupoux and Green (1997): listeners improve noticeably on speech compressed to 35% of
  its length within 5 to 10 sentences. It carries over to new talkers and related languages, so it is not
  only acoustic.
- **Longer training adds more, but it is more specific.** Multi-day training beats brief adaptation. The
  extra learning transfers less well to new talkers than the first few minutes do (Banai and colleagues,
  PLoS ONE 2012). Train on the narrators you mean to listen to.
- **It generalises across rates.** Learning at one compression helps at others, which makes a gradual ramp
  sensible.
- **Ramps work in practice.** Rightspeed raised the speed by 0.1x every two minutes; its author reached a
  comfortable 5.3x for audiobooks. Another regime alternates 10 minutes a step above comfortable with 10
  minutes back at the usual speed.
- **The ceiling moves a long way with practice.** Late-blind adults trained for months reached
  ultra-fast synthetic speech (16 to 20 syllables a second) and recruited visual cortex to do it. That
  is the best evidence that 5x is learnable. 8x would need either removing more than pauses or a better
  signal.
- **Adaptation fades between sessions,** so a short ramp-up at the start of each session helps more than
  starting cold at the top speed.

## Proposals for approval

Ordered by expected benefit per effort. "Evidence" is how well the idea is supported **at 5x to 8x**.

| # | Proposal | Where | Effort | Evidence |
|---|----------|-------|--------|----------|
| 1 | **Pause cap.** Shorten every pause to at most ~60 ms before compressing, so the speed goes to words. Adjustable, with an off switch. | speechwarp | Small | Good for the time saved. Its effect on comprehension at 6x+ is an inference from the syllable-rate arithmetic. |
| 2 | **Training ramp.** The player starts each session a few steps below the target and adds 0.1x every N minutes up to it. Optionally keeps creeping past the target, Rightspeed style. | player | Small | Good (adaptation studies, Rightspeed). |
| 3 | **Retune Speedy for high speeds.** Today a tense block can drop to 1x, so at 8x the slack blocks must run far faster than 8x. Add a floor such as R/2 for tense blocks, and sweep the floor and the tension weights with the blind A/B tool at 5x to 8x. | speechwarp | Small to medium | MACH1 is the best-supported method, but no one has tuned it this high. |
| 4 | **Rhythm repackaging.** After the pause cap, put back a regular 20 to 60 ms gap every syllable-sized chunk, compressing the speech a little harder to keep the overall speed. Experimental toggle. | speechwarp | Medium | Strong at 3x (Ghitza and Greenberg). Untested at 5x+, and the counter-intuitive one. |
| 5 | **ESOLA under Speedy** as an alternative to Sonic, compared blind. | speechwarp | Medium to large | Weak: better joins, unknown gain at high speed. |
| 6 | **Transcribe and re-voice.** Turn the audio into text on the device, then speak it with a fast synthetic voice, as screen-reader experts do at 700+ words a minute. Loses the narrator. A separate mode, not a speed-up. | player | Large | Good that people can learn it. Unknown whether audiobook listeners would want it. |
| 7 | Neural time-scale modification | - | Large | Not yet: tested only to 2x. |

Recommendation: do 1, 2 and 3 first, with a blind A/B test at 5x, 6.5x and 8x. Then try 4, since it is the
idea most likely to move the ceiling. Leave 5 to 7 until those results are in.

**Done in 0.2.0:** 1, 3 and 4, as options of the library (with keep overall speed, and a syllable counter for
measuring). A first measurement on synthetic speech is in
[How it works](how-it-works.md#a-first-evaluation): the pause cap gains about 6% slower words on top of
Speedy, the floor evens out the slowest parts, and rhythm costs a third faster words between its gaps. None of
it has been tested by listening yet.

## Sources

- Covell, Withgott, Slaney. [MACH1: Nonuniform time-scale modification of speech](https://www.mangolassi.org/covell/1997-060/). ICASSP 1998.
- [google/speedy](https://github.com/google/speedy): the MACH1 implementation speechwarp packages; evaluated at 3.5x.
- [Non-linear time compression of clear and normal speech at high rates](https://arxiv.org/html/1901.07239) (arXiv 1901.07239).
- Ghitza and Greenberg 2009, on silence insertion, as summarised in [Frontiers in Psychology 2013](https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2013.00138/full) and [PMC9938863](https://pmc.ncbi.nlm.nih.gov/articles/PMC9938863/).
- [Ultra-fast speech comprehension in blind subjects](https://bmcneurosci.biomedcentral.com/articles/10.1186/1471-2202-14-74), BMC Neuroscience 2013; [Comprehension of ultra-fast speech: blind vs. normally hearing](https://www.semanticscholar.org/paper/COMPREHENSION-OF-ULTRA-FAST-SPEECH-BLIND-VS.-Moos-Trouvain/d403dd5301eaff3c66d6ed3f015de47aee66e02f); [Training of ultra-fast speech comprehension in late-blind humans](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC3805979/).
- [Perceptual learning of time-compressed speech: more than rapid adaptation](https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0047099), PLoS ONE 2012; [Adaptation to time-compressed speech: phonological determinants](https://www.researchgate.net/publication/12434703_Adaptation_to_time-compressed_speech_Phonological_determinants).
- [ESOLA](https://arxiv.org/pdf/1801.06492) (arXiv 1801.06492).
- [STSM-FiLM](https://arxiv.org/abs/2510.02672) (arXiv 2510.02672), with Neural ATSM and DiffATSM.
- [Bungee's comparison of stretch techniques](https://bungee.parabolaresearch.com/compare-audio-stretch-tempo-pitch-change); [Rubber Band](https://github.com/breakfastquay/rubberband).
- [Overcast Smart Speed](https://www.paulingraham.com/overcast-smart-speed.html); [Rightspeed](https://techcrunch.com/2016/04/26/rightspeed-helps-you-devour-audio-books-at-a-terrifying-pace/).
