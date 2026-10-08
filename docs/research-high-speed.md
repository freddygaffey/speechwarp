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

**Done in 0.3.0:** rules that set the pause cap and the floor from the speed, so a training ramp needs no
recomputing ([How it works](how-it-works.md#rules-that-follow-the-speed)); the syllable counter on its own, for
players with another speed-up; and a listener trainer, below.

## How comprehension has been measured

The research on fast speech uses several measures, and they do not agree, because they ask different things
of the listener.

- **Intelligibility: saying back what was heard.** A sentence or a list of words is played once and the
  listener repeats it; the score is the share of words right. Foulke and Sticht's review (1969) of two decades
  of compressed-speech work separates this from comprehension: intelligibility of single words holds up to
  very high rates, while comprehension of connected speech starts to fall at about 275 words a minute and
  falls quickly past 350. Repeating back measures whether the sounds got through, not whether the meaning did,
  and it is the easiest measure to score on a phone: on-device speech recognition transcribes the answer and
  the words are compared.
- **Multiple-choice questions.** Questions about a passage, answered afterwards. Covell, Withgott and Slaney's
  evaluation of the algorithm behind Speedy used listening passages and questions in the style of the TOEFL
  (Test of English as a Foreign Language): comprehension at 2.5x to 4.2x with nonuniform compression was 5 to
  31 points better than with even compression. Questions need writing for each passage, and a good guesser
  scores 25% for nothing.
- **The sentence verification technique** (Royer and colleagues, from 1979). After a passage, the listener is
  shown sentences and says whether each was in it. Four kinds are mixed: *originals* (as heard), *paraphrases*
  (same meaning, different words: should be accepted), *meaning changes* (one or two words changed so that the
  meaning changes: should be rejected), and *distractors* (unrelated sentences of the same style: should be
  rejected). Accepting paraphrases and rejecting meaning changes cannot be done from memory of the sounds, so it
  measures meaning; the items are mechanical to write from the text itself, which makes them suitable for
  audiobooks; and chance is 50%, which the scoring allows for.
- **Delayed retests.** The same kind of items, some time later. A speed that is followed in the moment but not
  remembered is not much use for books. Retention at a day and at a week is the measure that matters for
  learning from audiobooks, and the hardest to collect.
- **Free recall.** Tell back everything you remember. The richest measure and the hardest to score, which is
  why it is rare in the speed literature; scoring it automatically against the text would need a language
  model.

## The test protocol

What the player runs, and what it passes to the trainer (all as plain numbers):

1. **Before a session: a threshold test.** About 25 short presentations, a sentence each at the rate the
   trainer asks for, scored by saying it back (intelligibility) and now and then by a verification item: two to
   three minutes. The result is the rate understood 75% of the time, with a 95% interval.
2. **During the session: a check every ten minutes or so.** One sentence said back, or one or two
   verification items about what was just heard, at the rate playing. These steer the tracking plan and are
   kept as data; they do not change the threshold.
3. **After the session: a second threshold test.** The difference from the first, over the hours listened, is
   the session's gain.
4. **A day and a week later: retention.** Verification items about the session's material, answered with no
   replay. The delay is passed in with the score.
5. **After sessions, optionally: a rating.** "How well did you follow?", 1 to 5, scaled to 0 to 1. It is kept as
   a weaker signal, not mixed in at full weight, because how well people think they follow fast speech is not
   a good guide to how well they do.

## The trainer's design

All in `src/trainer.c`, with no clock, storage or audio: the player passes its own timestamps and scores and
keeps its own log, which it replays into a new trainer (created with the same seed) to restore state.

**The unit is syllables a second heard**, the source's syllable rate times the speed. Thresholds in speed
would change with every narrator; in syllables a second, they carry over, and the player converts back by
dividing by the book's syllable rate (from the syllable counter).

**Weights.** Each kind of measure counts per item: a word said back 0.5 (the words of one sentence are not
independent), a verification item 1, a retention item 1, a rating 0.3. The caller can change them; the
defaults reflect how directly each measures understanding and how much each item can be trusted.

**a. Threshold: the psi method.** Adaptive staircases (up-down rules) are simple and robust, but they waste
trials at the start and give no interval. QUEST (Watson and Pelli, 1983) is Bayesian but assumes the slope
of the psychometric function is known; for speech rates it is not, and it varies between people. The psi
method (Kontsevich and Tyler, 1999) keeps a posterior over both the threshold and the slope and chooses each
presentation to reduce the expected uncertainty most. The trainer uses the psi-marginal variant (Prins, 2013),
which treats the slope as a nuisance and spends its trials on the threshold. The psychometric function is a
logistic in log rate, falling through 75% at the threshold, with a 4% lapse rate and each measure's guess rate
(0.5 for verification items). A score from n items counts as n weighted trials, which lets sentences scored
by the share of words right sit alongside yes-or-no items. On simulated listeners with thresholds from 7 to 30
syllables a second and slopes from 4 to 12, a test of 8-word sentences took about 25 presentations, the median
error was 2 to 3%, and the 95% interval held the true threshold in 159 of 160 runs.

**b. Session plans.** Four, as candidates to compare, each giving the rate to play from the threshold measured
before the session:

- *Steady*: the threshold plus a margin (10% by default). Training a little beyond what is comfortable is the
  common advice. Against it, Wilson and colleagues (2019) showed that for a broad class of learners the fastest
  learning comes at about 85% correct, which is below a 75% threshold; the margin may be set negative to test
  that.
- *Ramp*: start at 80% of the target and step up 2% of it every two minutes. What the player's training mode
  already does, after Rightspeed (0.1x every two minutes, its author reaching 5.3x). Adaptation to compressed
  speech is quick but fades between sessions (Dupoux and Green, 1997; Adank and Janse, 2009), so a short climb
  at the start of each session is expected to help.
- *Interval*: ten minutes 15% above the threshold, ten minutes 15% below, alternating. A regime reported by
  speed listeners; in learning research, alternating hard and easy practice is one of the "desirable
  difficulties" (Bjork, 1994), though not tested on fast speech.
- *Tracking*: start at the threshold and move after each in-session check, by a continuous form of
  Kaernbach's weighted up-down rule (1991): up a little after a good check, three times as much down after a
  bad one, so that it settles where the listener understands 75%. It follows the listener through fatigue and
  warm-up. In simulation it settled within 1% of the true threshold.

**c. Comparing plans: Thompson sampling.** Which plan trains fastest is the question the data has to answer,
so each new session's plan is chosen by Thompson sampling (Thompson, 1933; Russo and colleagues, 2018): draw
each plan's effect from its posterior and run the best draw. Plans that look good are run more, and uncertain
ones are still tried, in proportion to the chance that they are best. The model:

- A session's gain is ln(threshold after / threshold before), and its expected value is the hours listened
  times the plan's effect, times a learning-curve factor 1 / (1 + hours listened so far / H). Gains shrink as
  the listener improves (the power law of practice; Newell and Rosenbloom, 1981); without the factor,
  whichever plan happened to run early would look best. H, the hours by which gains halve, is unknown, so the
  model is fitted for H from 5 hours to never and the fits are averaged by their marginal likelihood. The trend
  reported is that H.
- Bayesian linear regression with a Normal-inverse-gamma prior, which is exact. The noise is that of two
  threshold tests (about 0.02 in variance of the log), so it does not grow with session length, which is why
  the hours scale the prediction rather than divide the outcome.
- **Retention as a cost.** A plan's utility is its effect less 0.2 times how far its retention falls below the
  best plan's, so 10 points of retention are worth 2% of threshold a hour. Retention is compared at like delays
  (up to a day and a half, or longer), as each plan's deviation from the pooled mean at that delay. Plans are
  compared on their effect before practice slows it, so the trade-off between speed and retention does not
  drift as training goes on.

What it exposes: the next plan; each plan's estimated gain a hour now, its standard deviation, its retention
and the probability that it is the best; and H. In simulation (four plans, 60 two-hour sessions, gains halving
by 20 hours, 1% forgotten a day), the plan that trained fastest lost on retention, and the bandit ranked the
plan that trained fastest without losing retention first for five of six simulated listeners.

How many sessions this needs: a threshold test has a standard deviation of about 5% in log rate, so with
sessions of two hours a difference of 1% a hour between plans takes dozens of sessions to show. The posterior
says how sure it is; the player should show the uncertainty, not just the ranking.

**d. Blind trials.** Moved from the player: which setting to compare next at a speed (the one with the fewest
trials in its whole-number speed band), which two of its values (the pair compared least), in random order;
and per band, each value's wins, losses, ties and mean score. A value is called the winner when it has met
every other value at least three times and, against each, the Bayes factor for "preferred" over "no
preference" reaches 20. Because the Bayes factor under no preference is a supermartingale, Ville's inequality
bounds the chance of ever naming a false winner by 5% each way however often the player asks. A preference
held 85% of the time was named after about 40 trials among three values; with no preference, a winner appeared
in 1 of 20 simulated runs of 90 trials.

## Findings

To be filled in from Fred's data: thresholds over time, the plans compared, retention at a day and a week, and
which settings won their blind trials at each speed.

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
- Foulke, Sticht. Review of research on the intelligibility and comprehension of accelerated speech.
  Psychological Bulletin 72(1), 1969.
- Royer, Hastings, Hook. A sentence verification technique for measuring reading comprehension. Journal of
  Reading Behavior 11, 1979; Royer and colleagues later applied it to listening.
- Kontsevich, Tyler. Bayesian adaptive estimation of psychometric slope and threshold. Vision Research 39, 1999.
- Prins. The psi-marginal adaptive method. Journal of Vision 13(7), 2013.
- Watson, Pelli. QUEST: a Bayesian adaptive psychometric method. Perception & Psychophysics 33, 1983.
- Kaernbach. Simple adaptive testing with the weighted up-down method. Perception & Psychophysics 49, 1991.
- Wilson, Shenhav, Straccia, Cohen. The Eighty Five Percent Rule for optimal learning. Nature Communications
  10, 2019.
- Bjork. Memory and metamemory considerations in the training of human beings, 1994 ("desirable difficulties").
- Adank, Janse. Perceptual learning of time-compressed and natural fast speech. JASA 126(5), 2009.
- Newell, Rosenbloom. Mechanisms of skill acquisition and the law of practice, 1981.
- Thompson. On the likelihood that one unknown probability exceeds another. Biometrika 25, 1933; Russo, Van
  Roy, Kazerouni, Osband, Wen. A tutorial on Thompson sampling, 2018.
- Ville. Étude critique de la notion de collectif, 1939 (Ville's inequality); Schönbrodt and colleagues,
  Sequential hypothesis testing with Bayes factors, Psychological Methods 22, 2017.
