/* speechwarp - nonlinear speed-up for speech.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * This file does the job of upstream's shim (third_party/speedy/soniclib.c): it cuts the input into 10 ms
 * blocks, has Speedy work out how tense the speech is in each one, and hands the block to Sonic with a speed
 * chosen from that. It feeds Speedy and Sonic exactly what the upstream shim does, and tests/test_parity.c
 * checks that. It differs where a library needs it to:
 *
 *   - nothing is printed, and running out of memory is reported instead of crashing;
 *   - flush keeps the last partial block, and the stream can be written to again afterwards;
 *   - it counts what goes in and what comes out, for speechwarp_position;
 *   - the average speed is held at the requested speed (see correct_speed below).
 *
 * The options for very high speeds add two stages, each skipped entirely while its option is off, so that the
 * defaults stay sample-for-sample the same as upstream:
 *
 *   input -> syllable counter -> pause cap (the gate) -> hold buffer -> Speedy and Sonic -> rhythm -> output
 */
#include "rename.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* SONIC_INTERNAL gives Sonic's functions the names they are built with in third_party_sonic.c. */
#define SONIC_INTERNAL 1
#include "../third_party/sonic/sonic.h"
#include "../third_party/speedy/speedy.h"

#include "internal.h"
#include "speechwarp.h"

/* Seconds of input over which the average speed is pulled back to the requested speed. */
#define CORRECTION_TIME 4.0

/* How far Speedy's own speeds fall short on speech, as a fraction of the speed-up (the speed less one). */
#define TYPICAL_SHORTFALL 0.17

/* The hold buffer, in blocks. Speedy looks kTemporalHysteresisFuture blocks ahead, so about 14 are held. */
#define HOLD_BLOCKS 32

#define MIN_SAMPLE_RATE 4000
#define MAX_SAMPLE_RATE 384000

/* The pause cap. A block is a pause if it is this far below the level of recent speech, or below the floor. */
#define SILENCE_BELOW_SPEECH_DB 30.0
#define SILENCE_FLOOR_DB -70.0
/* How quickly the level of recent speech forgets a loud passage, in dB a second. */
#define SPEECH_LEVEL_DECAY_DB 0.5
#define MIN_PAUSE_CAP 0.01f
#define MAX_PAUSE_CAP 1.0f

/* Rhythm. Gaps are placed at the quietest point within this fraction of a chunk either side of where the
 * regular rate puts them, and faded in and out over FADE_TIME seconds. */
#define MIN_RHYTHM_GAP 0.005f
#define MAX_RHYTHM_GAP 0.2f
#define MIN_RHYTHM_RATE 1.0f
#define MAX_RHYTHM_RATE 16.0f
#define DEFAULT_RHYTHM_RATE 5.0f
#define SEARCH_FRACTION 0.3
#define FADE_TIME 0.005

/* The syllable counter (see the section of that name). */
#define SYLLABLE_SMOOTHING 4
#define SYLLABLE_HISTORY 1024 /* more than a minute at the fastest rate it can count */
#define SYLLABLE_WINDOW 60.0
#define SYLLABLE_MINIMUM 10.0

/* After `in` frames of input had been given to Sonic, `out` frames of output had been produced. `in` counts
 * the input written, including any the pause cap left out. */
typedef struct {
  int64_t in;
  int64_t out;
} mark;

/* The pause cap left out `frames` frames of input just before the `at`th frame it passed on. */
typedef struct {
  int64_t at;
  int64_t frames;
} skip;

/* Rhythm put `frames` frames of silence into the output, starting at output frame `at`. */
typedef struct {
  int64_t at;
  int64_t frames;
} gap;

typedef struct {
  double b0, b1, b2, a1, a2;
  double x1, x2, y1, y2;
} biquad;

typedef struct {
  int sample_rate;
  int frame_length;
  biquad high_pass, low_pass, crossing_high_pass;

  int fill;
  double energy;
  int crossings;
  double last;
  int64_t frames_done; /* 10 ms frames finished */
  int64_t analysed;    /* input frames analysed */

  double recent_energy[SYLLABLE_SMOOTHING];
  double recent_crossings[SYLLABLE_SMOOTHING];
  double max_db;

  int rising;
  double extreme_db;
  int64_t extreme_at;
  double extreme_crossings;
  int64_t last_syllable;

  int64_t found[SYLLABLE_HISTORY]; /* input frame of each syllable, a ring */
  int found_head;
  int found_count;
} syllable_counter;

struct speechwarp_stream {
  sonicStream sonic; /* NULL once memory has run out in reset: the stream is then dead */
  speedyStream speedy;
  int sample_rate;
  int channels;
  float speed;
  float nonlinear;
  int correction; /* hold the average speed at `speed` */

  float pause_cap;
  int keep_speed;
  float speed_floor;
  float rhythm_gap;
  float rhythm_rate;

  int block;  /* frames in a block, 10 ms */
  int window; /* frames Speedy analyses at a time, 15 ms */
  int16_t* convert; /* one block, for converting float input */

  /* The pause cap. Input is cut into blocks in `partial`. The first half of each pause goes straight on; the
   * rest waits in `tail`, a queue of whole blocks, and what does not fit is left out. `resume` is the first
   * block left out, which is crossfaded into the start of the tail so that the cut does not click. */
  int16_t* partial;
  int partial_frames;
  int16_t* tail;
  int tail_blocks;
  int tail_capacity; /* in blocks */
  int16_t* resume;
  int have_resume;
  int pause_blocks; /* blocks of pause in a row so far */
  double speech_db;
  int64_t gated; /* frames passed on to the hold buffer */
  skip* skips;
  int skips_head;
  int skips_count;
  int skips_capacity;
  int64_t skipped; /* frames left out before the input given to Sonic so far */

  /* Input not yet given to Sonic, interleaved. It starts on a block boundary. */
  int16_t* hold;
  int hold_capacity; /* in frames, as are the next two */
  int hold_offset;
  int hold_frames;
  int16_t* mono; /* one window mixed down for Speedy */

  int analysing;           /* Speedy is choosing the speeds */
  int64_t blocks_analysed; /* windows given to Speedy */
  int64_t blocks_released; /* blocks Speedy has chosen a speed for */
  double excess;           /* seconds of output more than the requested speed would have produced */

  int64_t frames_written;
  int64_t frames_released; /* given to Sonic */
  int64_t frames_read;     /* by the caller, gaps included */
  int64_t sonic_taken;     /* frames taken out of Sonic */
  int64_t sonic_read;      /* of those, read by the caller */

  /* Rhythm. Output taken from Sonic waits in `pending` until the next gap has been placed, then moves to
   * `ready`, where the caller reads it. `since_gap` counts the output moved to `ready` since the last gap. */
  float* pending;
  int pending_frames;
  int pending_capacity;
  float* ready;
  int ready_offset;
  int ready_frames;
  int ready_capacity;
  int64_t since_gap;
  int ended; /* flushed, and nothing written since */
  gap* gaps;
  int gaps_head;
  int gaps_count;
  int gaps_capacity;

  /* marks[marks_head] is the newest mark at or before the read position. */
  mark* marks;
  int marks_head;
  int marks_count;
  int marks_capacity;

  syllable_counter syllables;
};

/* ---- Queues ---------------------------------------------------------------------------------------- */

/* Make room for one more item at the end of a queue whose live items start at *head. Returns 0 if memory ran
 * out. */
static int queue_room(void** items, size_t size, int* head, int* count, int* capacity) {
  if (*count < *capacity) {
    return 1;
  }
  if (*head > 0) {
    *count -= *head;
    memmove(*items, (char*)*items + (size_t)*head * size, (size_t)*count * size);
    *head = 0;
    return 1;
  } else {
    int grown_capacity = *capacity ? *capacity * 2 : 16;
    void* grown = realloc(*items, (size_t)grown_capacity * size);
    if (!grown) {
      return 0;
    }
    *items = grown;
    *capacity = grown_capacity;
    return 1;
  }
}

/* Make room for `frames` frames at the end of a float buffer of `channels` channels. */
static int float_room(float** buffer, int* capacity, int used, int frames, int channels) {
  if (used + frames > *capacity) {
    int grown_capacity = *capacity * 2 > used + frames ? *capacity * 2 : used + frames + 4096;
    float* grown = (float*)realloc(*buffer, (size_t)grown_capacity * channels * sizeof(float));
    if (!grown) {
      return 0;
    }
    *buffer = grown;
    *capacity = grown_capacity;
  }
  return 1;
}

/* ---- The syllable counter --------------------------------------------------------------------------
 *
 * Estimates how many syllables a second are spoken by finding syllable nuclei: peaks of loudness in voiced
 * sound, each separated from the last by a dip. This follows de Jong and Wempe, "Praat script to detect
 * syllable nuclei and measure speech rate automatically" (Behavior Research Methods, 2009). Ported from the
 * player this library was written for, where on macOS speech (five voices, 140 to 280 words a minute) it came
 * within -12% to +1% of the true count. */

/* A peak must stand this far above the dip before it, and fall this far after. de Jong and Wempe use 2 dB. */
#define SYLLABLE_DIP_DB 1.0
/* Peaks quieter than this below the loudest recent speech are background. */
#define SYLLABLE_SILENCE_BELOW_MAX_DB 25.0
#define SYLLABLE_MAX_DECAY_DB 0.5 /* a second */
#define SYLLABLE_FLOOR_DB -60.0
/* Vowels cross zero a few hundred to about two thousand times a second; hiss such as "s" far more. */
#define SYLLABLE_VOICED_CROSSINGS 3000.0
/* Even very fast speech does not put two syllables closer than this. */
#define SYLLABLE_MIN_GAP 0.06

/* A second-order Butterworth filter, from Robert Bristow-Johnson's cookbook. */
static void biquad_init(biquad* f, double rate, double frequency, int high) {
  double w = 2 * 3.141592653589793 * frequency / rate;
  double c = cos(w), alpha = sin(w) / (2 * sqrt(0.5));
  double a0 = 1 + alpha;
  if (high) {
    f->b0 = (1 + c) / 2 / a0;
    f->b1 = -(1 + c) / a0;
  } else {
    f->b0 = (1 - c) / 2 / a0;
    f->b1 = (1 - c) / a0;
  }
  f->b2 = f->b0;
  f->a1 = -2 * c / a0;
  f->a2 = (1 - alpha) / a0;
  f->x1 = f->x2 = f->y1 = f->y2 = 0;
}

static double biquad_run(biquad* f, double x) {
  double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
  f->x2 = f->x1;
  f->x1 = x;
  f->y2 = f->y1;
  f->y1 = y;
  return y;
}

static void syllables_reset(syllable_counter* c, int sample_rate) {
  memset(c, 0, sizeof(*c));
  c->sample_rate = sample_rate;
  c->frame_length = sample_rate / 100;
  biquad_init(&c->high_pass, sample_rate, 250, 1);
  biquad_init(&c->low_pass, sample_rate, sample_rate * 0.45 < 3000 ? sample_rate * 0.45 : 3000, 0);
  biquad_init(&c->crossing_high_pass, sample_rate, 100, 1);
  c->max_db = -HUGE_VAL;
  c->rising = 1;
  c->extreme_db = -HUGE_VAL;
  c->last_syllable = INT64_MIN / 2;
}

static void syllables_found(syllable_counter* c, int64_t at) {
  int64_t oldest = c->analysed - (int64_t)(SYLLABLE_WINDOW * c->sample_rate);
  while (c->found_count > 0 && c->found[c->found_head] < oldest) {
    c->found_head = (c->found_head + 1) % SYLLABLE_HISTORY;
    c->found_count--;
  }
  if (c->found_count == SYLLABLE_HISTORY) {
    c->found_head = (c->found_head + 1) % SYLLABLE_HISTORY;
    c->found_count--;
  }
  c->found[(c->found_head + c->found_count) % SYLLABLE_HISTORY] = at;
  c->found_count++;
}

static void syllables_end_frame(syllable_counter* c) {
  const double frame_seconds = 0.01;
  int slot = (int)(c->frames_done % SYLLABLE_SMOOTHING);
  double energy = 0, crossings = 0, db;
  int64_t centre;
  int j;

  c->recent_energy[slot] = c->energy / c->frame_length;
  c->recent_crossings[slot] = c->crossings / frame_seconds;
  c->fill = 0;
  c->energy = 0;
  c->crossings = 0;
  c->frames_done++;

  for (j = 0; j < SYLLABLE_SMOOTHING; j++) {
    energy += c->recent_energy[j];
    crossings += c->recent_crossings[j];
  }
  db = 10 * log10(energy / SYLLABLE_SMOOTHING + 1e-12);
  crossings /= SYLLABLE_SMOOTHING;
  /* The smoothed value is centred half a window back. */
  centre = c->analysed - (int64_t)c->frame_length * (SYLLABLE_SMOOTHING + 1) / 2;

  if (db > c->max_db - SYLLABLE_MAX_DECAY_DB * frame_seconds) {
    c->max_db = db;
  } else {
    c->max_db -= SYLLABLE_MAX_DECAY_DB * frame_seconds;
  }

  if (c->rising) {
    if (db > c->extreme_db) {
      c->extreme_db = db;
      c->extreme_at = centre;
      c->extreme_crossings = crossings;
    } else if (db < c->extreme_db - SYLLABLE_DIP_DB) {
      /* The peak is behind us: count it if it was loud, voiced and not too close to the last. */
      if (c->extreme_db > SYLLABLE_FLOOR_DB && c->extreme_db > c->max_db - SYLLABLE_SILENCE_BELOW_MAX_DB &&
          c->extreme_crossings < SYLLABLE_VOICED_CROSSINGS &&
          c->extreme_at - c->last_syllable >= (int64_t)(SYLLABLE_MIN_GAP * c->sample_rate)) {
        syllables_found(c, c->extreme_at);
        c->last_syllable = c->extreme_at;
      }
      c->rising = 0;
      c->extreme_db = db;
    }
  } else if (db < c->extreme_db) {
    c->extreme_db = db;
  } else if (db > c->extreme_db + SYLLABLE_DIP_DB) {
    c->rising = 1;
    c->extreme_db = db;
    c->extreme_at = centre;
    c->extreme_crossings = crossings;
  }
}

static void syllables_write(syllable_counter* c, const int16_t* in, int frames, int channels) {
  int i, k;
  for (i = 0; i < frames; i++) {
    double mono = 0, band, for_crossing;
    for (k = 0; k < channels; k++) {
      mono += *in++;
    }
    mono /= channels * 32768.0;
    band = biquad_run(&c->low_pass, biquad_run(&c->high_pass, mono));
    c->energy += band * band;
    for_crossing = biquad_run(&c->crossing_high_pass, mono);
    if ((for_crossing >= 0) != (c->last >= 0)) {
      c->crossings++;
    }
    c->last = for_crossing;
    c->analysed++;
    if (++c->fill == c->frame_length) {
      syllables_end_frame(c);
    }
  }
}

static double syllables_rate(const syllable_counter* c) {
  double analysed = (double)c->analysed / c->sample_rate;
  double span = analysed < SYLLABLE_WINDOW ? analysed : SYLLABLE_WINDOW;
  int64_t from = c->analysed - (int64_t)(span * c->sample_rate);
  int count = 0, i;

  if (analysed < SYLLABLE_MINIMUM) {
    return -1;
  }
  for (i = 0; i < c->found_count; i++) {
    if (c->found[(c->found_head + i) % SYLLABLE_HISTORY] >= from) {
      count++;
    }
  }
  return count / span;
}

/* ---- Marks and the average speed ---------------------------------------------------------------------- */

static int64_t frames_produced(const speechwarp_stream* s) {
  return s->sonic_taken + sonicSamplesAvailable(s->sonic);
}

/* Record how much output there is now that `in` frames of input have gone to Sonic. A mark that cannot be
 * stored only makes speechwarp_position coarser. */
static void add_mark(speechwarp_stream* s, int64_t in) {
  int64_t out = frames_produced(s);
  mark* last = &s->marks[s->marks_count - 1];

  if (out == last->out) {
    return; /* Sonic is still holding that input; the earlier mark is the truer one. */
  }
  if (!queue_room((void**)&s->marks, sizeof(mark), &s->marks_head, &s->marks_count, &s->marks_capacity)) {
    last = &s->marks[s->marks_count - 1];
    last->in = in;
    last->out = out;
    return;
  }
  s->marks[s->marks_count].in = in;
  s->marks[s->marks_count].out = out;
  s->marks_count++;
}

/* Whether trimmed pauses and inserted gaps are paid back, so that the speed may also be corrected downwards. */
static int keeping_speed(const speechwarp_stream* s) {
  return s->correction && s->keep_speed && (s->pause_cap > 0 || s->rhythm_gap > 0);
}

/* The share of the output that rhythm gaps take, and the frames of speech between two of them. */
static double gap_share(const speechwarp_stream* s, int* chunk_frames) {
  double period = s->sample_rate / s->rhythm_rate;
  double gap_frames = s->rhythm_gap * s->sample_rate;
  double chunk = period - gap_frames;
  double fade = FADE_TIME * s->sample_rate;

  if (chunk < gap_frames) chunk = gap_frames; /* gaps never take more than half the time */
  if (chunk < 4 * fade) chunk = 4 * fade;
  if (chunk_frames) *chunk_frames = (int)chunk;
  return gap_frames / (chunk + gap_frames);
}

/* Speedy slows down for consonants more than it hurries through vowels, so its own speeds average about 15%
 * under the one asked for on audiobooks. Upstream corrects for that by adding 0.1 to the speed for each second
 * the output has run over, which at 10x takes minutes of listening to settle. Here the gain is scaled so that
 * it settles within CORRECTION_TIME seconds of input at any speed. As upstream, it only ever speeds up: time
 * saved in a pause is kept, not spent by slowing the speech that follows.
 *
 * Keeping the overall speed (keeping_speed) changes that: time saved by the pause cap is subtracted from the
 * excess and time spent in rhythm gaps added to it, and the correction may then slow the speech too, though
 * never below 1x. */

/* Begin as if the output had already run over by as much as it settles at on typical speech, so that the
 * first few seconds are not slow. */
static void start_correction(speechwarp_stream* s) {
  double target = s->speed;
  double shortfall = 0;

  if (target > 1 && s->analysing) {
    shortfall = TYPICAL_SHORTFALL * (target - 1);
  }
  if (keeping_speed(s) && s->rhythm_gap > 0) {
    double share = gap_share(s, NULL);
    shortfall += target * share / (1 - share);
  }
  s->excess = shortfall * CORRECTION_TIME / (target * target);
  if (s->excess > CORRECTION_TIME / target) s->excess = CORRECTION_TIME / target;
}

static void limit_excess(speechwarp_stream* s) {
  double most = CORRECTION_TIME / s->speed; /* at most doubles the speed */
  double least = keeping_speed(s) ? -most : 0;
  if (s->excess < least) s->excess = least;
  if (s->excess > most) s->excess = most;
}

static float correct_speed(speechwarp_stream* s, float speed) {
  double target = s->speed;
  double gain = target * target / CORRECTION_TIME;
  double corrected = speed;

  if (s->correction) {
    corrected += gain * s->excess;
  }
  if (keeping_speed(s)) {
    double slowest = target < 1 ? target : 1;
    if (corrected < slowest) corrected = slowest;
  }
  if (corrected < SONIC_MIN_SPEED) corrected = SONIC_MIN_SPEED;
  if (corrected > SONIC_MAX_SPEED) corrected = SONIC_MAX_SPEED;

  s->excess += (double)s->block / s->sample_rate * (1.0 / corrected - 1.0 / target);
  if (keeping_speed(s) && s->rhythm_gap > 0) {
    /* The gaps that will go into this block's output: counted here rather than when they are put in, which
     * depends on when the output is read. */
    double share = gap_share(s, NULL);
    s->excess += (double)s->block / s->sample_rate / corrected * share / (1 - share);
  }
  limit_excess(s);
  return (float)corrected;
}

/* ---- Speedy and Sonic ----------------------------------------------------------------------------------- */

/* The input frame, counting what the pause cap left out, that follows the first `released` frames it passed
 * on. Called with `released` never decreasing. */
static int64_t source_frame(speechwarp_stream* s, int64_t released) {
  while (s->skips_head < s->skips_count && s->skips[s->skips_head].at <= released) {
    s->skipped += s->skips[s->skips_head].frames;
    s->skips_head++;
  }
  return released + s->skipped;
}

/* Give the first `frames` held frames to Sonic. A `speed` of 0 keeps Sonic's current speed. */
static int release(speechwarp_stream* s, int frames, float speed) {
  if (speed > 0) {
    sonicSetSpeed(s->sonic, speed);
  }
  if (!sonicWriteShortToStream(s->sonic, s->hold + (size_t)s->hold_offset * s->channels, frames)) {
    return 0;
  }
  s->hold_offset += frames;
  s->hold_frames -= frames;
  s->frames_released += frames;
  add_mark(s, source_frame(s, s->frames_released));
  return 1;
}

/* Analyse the window that starts `blocks_ahead` blocks into the hold buffer. */
static void analyse(speechwarp_stream* s, int blocks_ahead) {
  const int16_t* in = s->hold + ((size_t)s->hold_offset + (size_t)blocks_ahead * s->block) * s->channels;
  int channels = s->channels;
  int i, c;

  for (i = 0; i < s->window; i++) {
    int sum = 0;
    for (c = 0; c < channels; c++) {
      sum += *in++;
    }
    s->mono[i] = (int16_t)(sum / channels);
  }
  /* Upstream labels each window with the index of the block being filled when it is complete. */
  speedyAddDataShort(s->speedy, s->mono, s->blocks_analysed + s->window / s->block);
  s->blocks_analysed++;
}

/* Give Sonic every block that is ready. Returns 0 if memory ran out. */
static int pump(speechwarp_stream* s) {
  for (;;) {
    if (s->analysing) {
      int blocks_ahead = (int)(s->blocks_analysed - s->blocks_released);
      float tension;

      /* Upstream waits for one frame more than the window holds; match it so the timing is the same. */
      if (s->hold_frames < blocks_ahead * s->block + s->window + 1) {
        return 1;
      }
      analyse(s, blocks_ahead);
      if (speedyComputeTension(s->speedy, s->blocks_released, &tension)) {
        float speed = speedyComputeSpeedFromTension(tension, s->speed, 0.0f, s->speedy);
        if (s->speed_floor > 0 && s->speed > 1 && speed < s->speed_floor * s->speed) {
          speed = s->speed_floor * s->speed;
        }
        speed = speed * s->nonlinear + s->speed * (1.0f - s->nonlinear);
        if (!release(s, s->block, correct_speed(s, speed))) {
          return 0;
        }
        s->blocks_released++;
      }
    } else {
      if (s->hold_frames < s->block) {
        return 1;
      }
      if (!release(s, s->block, keeping_speed(s) ? correct_speed(s, s->speed) : s->speed)) {
        return 0;
      }
    }
  }
}

/* Add frames to the hold buffer and give Sonic what is ready. */
static int hold_write(speechwarp_stream* s, const int16_t* in, int frames) {
  while (frames > 0) {
    int room = s->hold_capacity - s->hold_offset - s->hold_frames;
    int take;
    size_t count;

    if (room == 0) {
      memmove(s->hold, s->hold + (size_t)s->hold_offset * s->channels,
              (size_t)s->hold_frames * s->channels * sizeof(int16_t));
      s->hold_offset = 0;
      room = s->hold_capacity - s->hold_frames;
    }
    take = frames < room ? frames : room;
    count = (size_t)take * s->channels;
    memcpy(s->hold + ((size_t)s->hold_offset + s->hold_frames) * s->channels, in, count * sizeof(int16_t));
    in += count;
    s->hold_frames += take;
    s->gated += take;
    frames -= take;
    if (!pump(s)) {
      return 0;
    }
  }
  return 1;
}

/* ---- The pause cap ------------------------------------------------------------------------------------- */

static int pause_cap_blocks(const speechwarp_stream* s) {
  int blocks = (int)(s->pause_cap * s->sample_rate / s->block + 0.5);
  return blocks < 1 ? 1 : blocks;
}

/* Whether a block is part of a pause, judged against the level of recent speech. */
static int is_pause(speechwarp_stream* s, const int16_t* in) {
  size_t count = (size_t)s->block * s->channels, i;
  double sum = 0, db;
  int pause;

  for (i = 0; i < count; i++) {
    sum += (double)in[i] * in[i];
  }
  db = 10 * log10(sum / count / (32768.0 * 32768.0) + 1e-12);
  pause = db < SILENCE_FLOOR_DB || db < s->speech_db - SILENCE_BELOW_SPEECH_DB;
  if (db > s->speech_db) {
    s->speech_db = db;
  } else {
    s->speech_db -= SPEECH_LEVEL_DECAY_DB * s->block / s->sample_rate;
  }
  return pause;
}

/* Leave out the oldest block of the tail. */
static int drop_tail_block(speechwarp_stream* s) {
  size_t block_samples = (size_t)s->block * s->channels;
  skip* last = s->skips_count > s->skips_head ? &s->skips[s->skips_count - 1] : NULL;

  if (!s->have_resume) {
    memcpy(s->resume, s->tail, block_samples * sizeof(int16_t));
    s->have_resume = 1;
  }
  memmove(s->tail, s->tail + block_samples, (size_t)(s->tail_blocks - 1) * block_samples * sizeof(int16_t));
  s->tail_blocks--;

  if (last && last->at == s->gated) {
    last->frames += s->block;
  } else {
    if (!queue_room((void**)&s->skips, sizeof(skip), &s->skips_head, &s->skips_count, &s->skips_capacity)) {
      return 0;
    }
    s->skips[s->skips_count].at = s->gated;
    s->skips[s->skips_count].frames = s->block;
    s->skips_count++;
  }
  if (keeping_speed(s)) {
    s->excess -= (double)s->block / s->sample_rate / s->speed;
    limit_excess(s);
  }
  return 1;
}

/* Pass the tail on, crossfading it from where the cut was made. */
static int release_tail(speechwarp_stream* s) {
  int blocks = s->tail_blocks;

  if (blocks == 0) {
    return 1;
  }
  if (s->have_resume) {
    int i, c;
    for (i = 0; i < s->block; i++) {
      double fade = (i + 0.5) / s->block;
      for (c = 0; c < s->channels; c++) {
        size_t k = (size_t)i * s->channels + c;
        s->tail[k] = (int16_t)lrint(s->resume[k] * (1 - fade) + s->tail[k] * fade);
      }
    }
    s->have_resume = 0;
  }
  s->tail_blocks = 0;
  return hold_write(s, s->tail, blocks * s->block);
}

/* Route one whole block, in `partial`. */
static int gate_block(speechwarp_stream* s) {
  int blocks = pause_cap_blocks(s);
  int head = blocks / 2, tail = blocks - head;

  if (s->pause_cap <= 0 || !is_pause(s, s->partial)) {
    s->pause_blocks = 0;
    return release_tail(s) && hold_write(s, s->partial, s->block);
  }
  s->pause_blocks++;
  if (s->pause_blocks <= head) {
    return release_tail(s) && hold_write(s, s->partial, s->block);
  }
  if (tail > s->tail_capacity) tail = s->tail_capacity;
  while (s->tail_blocks >= tail) {
    if (!drop_tail_block(s)) {
      return 0;
    }
  }
  memcpy(s->tail + (size_t)s->tail_blocks * s->block * s->channels, s->partial,
         (size_t)s->block * s->channels * sizeof(int16_t));
  s->tail_blocks++;
  return 1;
}

static int gate_write(speechwarp_stream* s, const int16_t* in, int frames) {
  if (s->pause_cap <= 0 && s->partial_frames == 0 && s->tail_blocks == 0) {
    return hold_write(s, in, frames);
  }
  while (frames > 0) {
    int take = s->block - s->partial_frames < frames ? s->block - s->partial_frames : frames;
    memcpy(s->partial + (size_t)s->partial_frames * s->channels, in, (size_t)take * s->channels * sizeof(int16_t));
    s->partial_frames += take;
    in += (size_t)take * s->channels;
    frames -= take;
    if (s->partial_frames == s->block) {
      s->partial_frames = 0;
      if (!gate_block(s)) {
        return 0;
      }
    }
  }
  return 1;
}

/* Pass on everything the pause cap is holding, at the end of the input. */
static int gate_flush(speechwarp_stream* s) {
  int frames = s->partial_frames;
  s->partial_frames = 0;
  s->pause_blocks = 0;
  return release_tail(s) && hold_write(s, s->partial, frames);
}

/* ---- Rhythm -------------------------------------------------------------------------------------------- */

static int rhythm_in_use(const speechwarp_stream* s) { return s->rhythm_gap > 0 || s->pending_frames > 0; }

/* Move the first `frames` pending frames to `ready`. */
static int move_to_ready(speechwarp_stream* s, int frames) {
  size_t samples = (size_t)frames * s->channels;
  if (s->ready_offset > 0 && s->ready_offset + s->ready_frames + frames > s->ready_capacity) {
    memmove(s->ready, s->ready + (size_t)s->ready_offset * s->channels,
            (size_t)s->ready_frames * s->channels * sizeof(float));
    s->ready_offset = 0;
  }
  if (!float_room(&s->ready, &s->ready_capacity, s->ready_offset + s->ready_frames, frames, s->channels)) {
    return 0;
  }
  memcpy(s->ready + (size_t)(s->ready_offset + s->ready_frames) * s->channels, s->pending, samples * sizeof(float));
  s->ready_frames += frames;
  s->pending_frames -= frames;
  memmove(s->pending, s->pending + samples, (size_t)s->pending_frames * s->channels * sizeof(float));
  s->since_gap += frames;
  return 1;
}

static int add_gap(speechwarp_stream* s, int frames) {
  int64_t at = s->frames_read + s->ready_frames;
  if (!queue_room((void**)&s->gaps, sizeof(gap), &s->gaps_head, &s->gaps_count, &s->gaps_capacity) ||
      !float_room(&s->ready, &s->ready_capacity, s->ready_offset + s->ready_frames, frames, s->channels)) {
    return 0;
  }
  memset(s->ready + (size_t)(s->ready_offset + s->ready_frames) * s->channels, 0,
         (size_t)frames * s->channels * sizeof(float));
  s->ready_frames += frames;
  s->gaps[s->gaps_count].at = at;
  s->gaps[s->gaps_count].frames = frames;
  s->gaps_count++;
  s->since_gap = 0;
  return 1;
}

/* Energy of the pending output from `from` to `to`. */
static double energy(const speechwarp_stream* s, int from, int to) {
  const float* p = s->pending + (size_t)from * s->channels;
  size_t count = (size_t)(to - from) * s->channels, i;
  double sum = 0;
  for (i = 0; i < count; i++) {
    sum += (double)p[i] * p[i];
  }
  return sum;
}

/* Take Sonic's output into the rhythm stage, and place gaps in it where there is enough to choose from. */
static int rhythm_fill(speechwarp_stream* s) {
  int available, chunk, fade, gap_frames, search;

  if (!rhythm_in_use(s)) {
    return 1;
  }
  available = sonicSamplesAvailable(s->sonic);
  if (available > 0) {
    if (!float_room(&s->pending, &s->pending_capacity, s->pending_frames, available, s->channels)) {
      return 0;
    }
    available = sonicReadFloatFromStream(s->sonic, s->pending + (size_t)s->pending_frames * s->channels, available);
    s->pending_frames += available;
    s->sonic_taken += available;
  }

  gap_share(s, &chunk);
  fade = (int)(FADE_TIME * s->sample_rate);
  gap_frames = (int)(s->rhythm_gap * s->sample_rate + 0.5);
  search = (int)(chunk * SEARCH_FRACTION);
  if (fade < 1) fade = 1;

  for (;;) {
    int earliest = (int)(chunk - search - s->since_gap); /* where the next gap may go, in pending */
    int latest = (int)(chunk + search - s->since_gap);

    if (s->rhythm_gap <= 0 || (s->ended && s->pending_frames < latest + fade)) {
      return move_to_ready(s, s->pending_frames); /* no gap after the end */
    }
    if (earliest < fade) earliest = fade;
    if (latest < earliest) latest = earliest;
    if (s->pending_frames >= latest + fade) {
      /* The quietest point, preferring one near where the regular rate puts it. */
      int nominal = (int)(chunk - s->since_gap), best = earliest, at, i, c;
      int step = fade / 2 > 0 ? fade / 2 : 1;
      double best_score = HUGE_VAL;
      for (at = earliest; at <= latest; at += step) {
        double distance = fabs((double)(at - nominal)) / (search > 0 ? search : 1);
        double score = (energy(s, at - fade, at + fade) + 1e-9) * (1 + distance);
        if (score < best_score) {
          best_score = score;
          best = at;
        }
      }
      for (i = 0; i < fade; i++) {
        float out = (float)(0.5 + 0.5 * cos(3.141592653589793 * (i + 0.5) / fade));
        for (c = 0; c < s->channels; c++) {
          s->pending[(size_t)(best - fade + i) * s->channels + c] *= out;
          s->pending[(size_t)(best + i) * s->channels + c] *= 1 - out;
        }
      }
      if (!move_to_ready(s, best) || !add_gap(s, gap_frames)) {
        return 0;
      }
      continue;
    }
    /* Not enough yet to choose: what lies before the earliest point, and its fade, can go now. */
    if (earliest - fade > 0) {
      int frames = earliest - fade < s->pending_frames ? earliest - fade : s->pending_frames;
      return move_to_ready(s, frames);
    }
    return 1;
  }
}

/* ---- The stream ---------------------------------------------------------------------------------------- */

static void destroy_engines(speechwarp_stream* s) {
  if (s->sonic) sonicDestroyStream(s->sonic);
  if (s->speedy) speedyDestroyStream(s->speedy);
  s->sonic = NULL;
  s->speedy = NULL;
}

static int create_engines(speechwarp_stream* s) {
  s->sonic = sonicCreateStream(s->sample_rate, s->channels);
  s->speedy = speedyCreateStream(s->sample_rate);
  if (!s->sonic || !s->speedy) {
    destroy_engines(s);
    return 0;
  }
  return 1;
}

static void clear_counters(speechwarp_stream* s) {
  s->partial_frames = 0;
  s->tail_blocks = 0;
  s->have_resume = 0;
  s->pause_blocks = 0;
  s->speech_db = -120;
  s->gated = 0;
  s->skips_head = 0;
  s->skips_count = 0;
  s->skipped = 0;
  s->hold_offset = 0;
  s->hold_frames = 0;
  s->blocks_analysed = 0;
  s->blocks_released = 0;
  start_correction(s);
  s->frames_written = 0;
  s->frames_released = 0;
  s->frames_read = 0;
  s->sonic_taken = 0;
  s->sonic_read = 0;
  s->pending_frames = 0;
  s->ready_offset = 0;
  s->ready_frames = 0;
  s->since_gap = 0;
  s->ended = 0;
  s->gaps_head = 0;
  s->gaps_count = 0;
  s->marks_head = 0;
  s->marks_count = 1;
  s->marks[0].in = 0;
  s->marks[0].out = 0;
  syllables_reset(&s->syllables, s->sample_rate);
}

const char* speechwarp_version(void) { return SPEECHWARP_VERSION; }

speechwarp_stream* speechwarp_create(int sample_rate, int channels) {
  speechwarp_stream* s;
  size_t block_bytes;

  if (sample_rate < MIN_SAMPLE_RATE || sample_rate > MAX_SAMPLE_RATE || channels < SONIC_MIN_CHANNELS ||
      channels > SONIC_MAX_CHANNELS) {
    return NULL;
  }
  s = (speechwarp_stream*)calloc(1, sizeof(*s));
  if (!s) {
    return NULL;
  }
  s->sample_rate = sample_rate;
  s->channels = channels;
  s->speed = 1.0f;
  s->nonlinear = 1.0f;
  s->analysing = 1;
  s->correction = 1;
  s->keep_speed = 1;
  s->rhythm_rate = DEFAULT_RHYTHM_RATE;
  if (!create_engines(s)) {
    free(s);
    return NULL;
  }
  s->block = speedyInputFrameStep(s->speedy);
  s->window = speedyInputFrameSize(s->speedy);
  s->hold_capacity = HOLD_BLOCKS * s->block;
  s->marks_capacity = 64;
  block_bytes = (size_t)s->block * channels * sizeof(int16_t);
  s->hold = (int16_t*)malloc((size_t)s->hold_capacity * channels * sizeof(int16_t));
  s->mono = (int16_t*)malloc((size_t)s->window * sizeof(int16_t));
  s->marks = (mark*)malloc((size_t)s->marks_capacity * sizeof(mark));
  s->convert = (int16_t*)malloc(block_bytes);
  s->partial = (int16_t*)malloc(block_bytes);
  s->resume = (int16_t*)malloc(block_bytes);
  if (!s->hold || !s->mono || !s->marks || !s->convert || !s->partial || !s->resume) {
    speechwarp_destroy(s);
    return NULL;
  }
  clear_counters(s);
  return s;
}

void speechwarp_destroy(speechwarp_stream* stream) {
  if (!stream) {
    return;
  }
  destroy_engines(stream);
  free(stream->hold);
  free(stream->mono);
  free(stream->marks);
  free(stream->convert);
  free(stream->partial);
  free(stream->tail);
  free(stream->resume);
  free(stream->skips);
  free(stream->pending);
  free(stream->ready);
  free(stream->gaps);
  free(stream);
}

void speechwarp_set_speed(speechwarp_stream* stream, float speed) {
  if (!stream || !(speed > 0)) {
    return;
  }
  if (speed < SPEECHWARP_MIN_SPEED) speed = SPEECHWARP_MIN_SPEED;
  if (speed > SPEECHWARP_MAX_SPEED) speed = SPEECHWARP_MAX_SPEED;
  if (speed != stream->speed) {
    stream->speed = speed;
    start_correction(stream);
  }
  if (stream->sonic) {
    sonicSetSpeed(stream->sonic, speed); /* until Speedy chooses one */
  }
}

float speechwarp_get_speed(const speechwarp_stream* stream) { return stream ? stream->speed : 0; }

void speechwarp_set_nonlinear(speechwarp_stream* stream, float amount) {
  if (!stream || amount != amount) {
    return;
  }
  if (amount < 0) amount = 0;
  if (amount > 1) amount = 1;
  stream->nonlinear = amount;
  if (amount > 0 && !stream->analysing) {
    /* Start on whatever is held, which is less than a block. */
    stream->analysing = 1;
    stream->blocks_released = stream->blocks_analysed;
    start_correction(stream);
  } else if (amount == 0 && stream->analysing) {
    /* The blocks held for Speedy's look-ahead go out at the even speed. */
    stream->analysing = 0;
    if (stream->sonic) {
      pump(stream);
      rhythm_fill(stream);
    }
  }
}

float speechwarp_get_nonlinear(const speechwarp_stream* stream) { return stream ? stream->nonlinear : 0; }

void speechwarp_set_pause_cap(speechwarp_stream* stream, float seconds) {
  int blocks;
  if (!stream || seconds != seconds) {
    return;
  }
  if (seconds <= 0) {
    stream->pause_cap = 0;
    return;
  }
  if (seconds < MIN_PAUSE_CAP) seconds = MIN_PAUSE_CAP;
  if (seconds > MAX_PAUSE_CAP) seconds = MAX_PAUSE_CAP;
  stream->pause_cap = seconds;
  blocks = pause_cap_blocks(stream);
  blocks = blocks - blocks / 2;
  if (blocks > stream->tail_capacity) {
    int16_t* grown = (int16_t*)realloc(stream->tail, (size_t)blocks * stream->block * stream->channels * sizeof(int16_t));
    if (!grown) {
      stream->pause_cap = 0;
      return;
    }
    stream->tail = grown;
    stream->tail_capacity = blocks;
  }
}

float speechwarp_get_pause_cap(const speechwarp_stream* stream) { return stream ? stream->pause_cap : 0; }

void speechwarp_set_keep_speed(speechwarp_stream* stream, int enabled) {
  if (stream) {
    stream->keep_speed = enabled != 0;
    limit_excess(stream);
  }
}

int speechwarp_get_keep_speed(const speechwarp_stream* stream) { return stream ? stream->keep_speed : 0; }

void speechwarp_set_speed_floor(speechwarp_stream* stream, float fraction) {
  if (!stream || fraction != fraction) {
    return;
  }
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  stream->speed_floor = fraction;
}

float speechwarp_get_speed_floor(const speechwarp_stream* stream) { return stream ? stream->speed_floor : 0; }

void speechwarp_set_rhythm_gap(speechwarp_stream* stream, float seconds) {
  if (!stream || seconds != seconds) {
    return;
  }
  if (seconds <= 0) {
    seconds = 0;
  } else {
    if (seconds < MIN_RHYTHM_GAP) seconds = MIN_RHYTHM_GAP;
    if (seconds > MAX_RHYTHM_GAP) seconds = MAX_RHYTHM_GAP;
  }
  if (seconds > 0 && stream->rhythm_gap <= 0) {
    stream->since_gap = 0;
  }
  stream->rhythm_gap = seconds;
  if (stream->sonic) {
    rhythm_fill(stream);
  }
}

float speechwarp_get_rhythm_gap(const speechwarp_stream* stream) { return stream ? stream->rhythm_gap : 0; }

void speechwarp_set_rhythm_rate(speechwarp_stream* stream, float per_second) {
  if (!stream || !(per_second > 0)) {
    return;
  }
  if (per_second < MIN_RHYTHM_RATE) per_second = MIN_RHYTHM_RATE;
  if (per_second > MAX_RHYTHM_RATE) per_second = MAX_RHYTHM_RATE;
  stream->rhythm_rate = per_second;
}

float speechwarp_get_rhythm_rate(const speechwarp_stream* stream) { return stream ? stream->rhythm_rate : 0; }

double speechwarp_syllable_rate(const speechwarp_stream* stream) {
  return stream ? syllables_rate(&stream->syllables) : -1;
}

static int16_t float_to_i16(float x) {
  if (x != x) return 0;
  if (x < -1.0f) x = -1.0f;
  if (x > 1.0f) x = 1.0f;
  return (int16_t)lrintf(x * 32767.0f);
}

static int write_frames(speechwarp_stream* s, const float* floats, const int16_t* ints, int frames) {
  if (!s || !s->sonic || frames < 0 || (frames > 0 && !floats && !ints)) {
    return 0;
  }
  s->ended = 0;
  while (frames > 0) {
    int take = frames < s->block ? frames : s->block;
    size_t count = (size_t)take * s->channels;
    const int16_t* in;

    if (ints) {
      in = ints;
      ints += count;
    } else {
      size_t i;
      for (i = 0; i < count; i++) {
        s->convert[i] = float_to_i16(floats[i]);
      }
      floats += count;
      in = s->convert;
    }
    syllables_write(&s->syllables, in, take, s->channels);
    s->frames_written += take;
    frames -= take;
    if (!gate_write(s, in, take)) {
      return 0;
    }
  }
  return rhythm_fill(s);
}

int speechwarp_write(speechwarp_stream* stream, const float* samples, int frames) {
  return write_frames(stream, samples, NULL, frames);
}

int speechwarp_write_i16(speechwarp_stream* stream, const int16_t* samples, int frames) {
  return write_frames(stream, NULL, samples, frames);
}

/* Count `frames` frames read from `ready`, of which those inside gaps came from no input. */
static void count_ready_read(speechwarp_stream* s, int frames) {
  int64_t from = s->frames_read, to = from + frames;
  int64_t speech = frames;

  while (s->gaps_head < s->gaps_count) {
    const gap* g = &s->gaps[s->gaps_head];
    int64_t start = g->at > from ? g->at : from;
    int64_t end = g->at + g->frames < to ? g->at + g->frames : to;
    if (g->at >= to) {
      break;
    }
    if (end > start) {
      speech -= end - start;
    }
    if (g->at + g->frames > to) {
      break; /* the rest of this gap comes in the next read */
    }
    s->gaps_head++;
  }
  s->frames_read = to;
  s->sonic_read += speech;
}

static int read_frames(speechwarp_stream* s, float* floats, int16_t* ints, int max_frames) {
  int done = 0;

  if (!s || !s->sonic || (!floats && !ints) || max_frames <= 0) {
    return 0;
  }
  rhythm_fill(s);
  if (s->ready_frames > 0) {
    int n = s->ready_frames < max_frames ? s->ready_frames : max_frames;
    const float* from = s->ready + (size_t)s->ready_offset * s->channels;
    size_t count = (size_t)n * s->channels, i;
    if (floats) {
      memcpy(floats, from, count * sizeof(float));
      floats += count;
    } else {
      for (i = 0; i < count; i++) {
        ints[i] = float_to_i16(from[i]);
      }
      ints += count;
    }
    s->ready_offset += n;
    s->ready_frames -= n;
    if (s->ready_frames == 0) {
      s->ready_offset = 0;
    }
    count_ready_read(s, n);
    done = n;
  }
  if (done < max_frames && !rhythm_in_use(s)) {
    int n = floats ? sonicReadFloatFromStream(s->sonic, floats, max_frames - done)
                   : sonicReadShortFromStream(s->sonic, ints, max_frames - done);
    s->sonic_taken += n;
    s->sonic_read += n;
    s->frames_read += n;
    done += n;
  }
  while (s->marks_head + 1 < s->marks_count && s->marks[s->marks_head + 1].out <= s->sonic_read) {
    s->marks_head++;
  }
  return done;
}

int speechwarp_read(speechwarp_stream* stream, float* samples, int max_frames) {
  return read_frames(stream, samples, NULL, max_frames);
}

int speechwarp_read_i16(speechwarp_stream* stream, int16_t* samples, int max_frames) {
  return read_frames(stream, NULL, samples, max_frames);
}

int speechwarp_available(const speechwarp_stream* stream) {
  if (!stream || !stream->sonic) {
    return 0;
  }
  return stream->ready_frames + (rhythm_in_use(stream) ? 0 : sonicSamplesAvailable(stream->sonic));
}

int speechwarp_flush(speechwarp_stream* stream) {
  speechwarp_stream* s = stream;
  mark* last;

  if (!s || !s->sonic) {
    return 0;
  }
  if (!gate_flush(s)) {
    return 0;
  }
  /* What Speedy has not chosen a speed for goes out at the speed of the last block it did, as upstream, and
   * a block at a time, so that Sonic rounds its sums the same way. */
  while (s->hold_frames > 0) {
    int frames = s->hold_frames < s->block ? s->hold_frames : s->block;
    if (!release(s, frames, s->analysing ? 0 : s->speed)) {
      return 0;
    }
  }
  if (!sonicFlushStream(s->sonic)) {
    return 0;
  }
  s->hold_offset = 0;
  s->blocks_released = s->blocks_analysed;

  /* All the input is now accounted for, so reading to the end must arrive at the end. */
  add_mark(s, s->frames_written);
  last = &s->marks[s->marks_count - 1];
  last->in = s->frames_written;

  s->ended = 1;
  return rhythm_fill(s);
}

void speechwarp_reset(speechwarp_stream* stream) {
  if (!stream) {
    return;
  }
  /* Neither Sonic nor Speedy can be emptied in place. If this runs out of memory the stream stays dead. */
  destroy_engines(stream);
  create_engines(stream);
  clear_counters(stream);
}

int64_t speechwarp_position(const speechwarp_stream* stream) {
  const mark* before;
  const mark* after;

  if (!stream) {
    return 0;
  }
  before = &stream->marks[stream->marks_head];
  if (stream->marks_head + 1 == stream->marks_count) {
    return before->in;
  }
  after = before + 1;
  return before->in +
         (after->in - before->in) * (stream->sonic_read - before->out) / (after->out - before->out);
}

void speechwarp_priv_set_speed_correction(speechwarp_stream* stream, int enabled) {
  stream->correction = enabled;
  start_correction(stream);
}
