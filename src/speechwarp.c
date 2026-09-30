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

/* After `in` frames of input had been given to Sonic, `out` frames of output had been produced. */
typedef struct {
  int64_t in;
  int64_t out;
} mark;

struct speechwarp_stream {
  sonicStream sonic; /* NULL once memory has run out in reset: the stream is then dead */
  speedyStream speedy;
  int sample_rate;
  int channels;
  float speed;
  float nonlinear;
  int correction; /* hold the average speed at `speed` */

  int block;  /* frames in a block, 10 ms */
  int window; /* frames Speedy analyses at a time, 15 ms */

  /* Input not yet given to Sonic, interleaved. It starts on a block boundary. */
  int16_t* hold;
  int hold_capacity; /* in frames, as are the next two */
  int hold_offset;
  int hold_frames;
  int16_t* mono; /* one window mixed down for Speedy */

  int analysing;          /* Speedy is choosing the speeds */
  int64_t blocks_analysed; /* windows given to Speedy */
  int64_t blocks_released; /* blocks Speedy has chosen a speed for */
  double excess;          /* seconds of output more than the requested speed would have produced */

  int64_t frames_written;
  int64_t frames_released; /* given to Sonic */
  int64_t frames_read;

  /* marks[marks_head] is the newest mark at or before the read position. */
  mark* marks;
  int marks_head;
  int marks_count;
  int marks_capacity;
};

static int64_t frames_produced(const speechwarp_stream* s) {
  return s->frames_read + sonicSamplesAvailable(s->sonic);
}

/* Record how much output there is now that `in` frames have gone to Sonic. A mark that cannot be stored only
 * makes speechwarp_position coarser. */
static void add_mark(speechwarp_stream* s, int64_t in) {
  int64_t out = frames_produced(s);
  mark* last = &s->marks[s->marks_count - 1];

  if (out == last->out) {
    return; /* Sonic is still holding that input; the earlier mark is the truer one. */
  }
  if (s->marks_count == s->marks_capacity) {
    if (s->marks_head > 0) {
      s->marks_count -= s->marks_head;
      memmove(s->marks, s->marks + s->marks_head, (size_t)s->marks_count * sizeof(mark));
      s->marks_head = 0;
    } else {
      mark* grown = (mark*)realloc(s->marks, (size_t)s->marks_capacity * 2 * sizeof(mark));
      if (!grown) {
        last = &s->marks[s->marks_count - 1];
        last->in = in;
        last->out = out;
        return;
      }
      s->marks = grown;
      s->marks_capacity *= 2;
    }
  }
  s->marks[s->marks_count].in = in;
  s->marks[s->marks_count].out = out;
  s->marks_count++;
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
  add_mark(s, s->frames_released);
  return 1;
}

/* Speedy slows down for consonants more than it hurries through vowels, so its own speeds average about 15%
 * under the one asked for on audiobooks. Upstream corrects for that by adding 0.1 to the speed for each second
 * the output has run over, which at 10x takes minutes of listening to settle. Here the gain is scaled so that
 * it settles within CORRECTION_TIME seconds of input at any speed. As upstream, it only ever speeds up: time
 * saved in a pause is kept, not spent by slowing the speech that follows. */

/* Begin as if the output had already run over by as much as it settles at on typical speech, so that the
 * first few seconds are not slow. */
static void start_correction(speechwarp_stream* s) {
  double target = s->speed;
  s->excess = target > 1 ? TYPICAL_SHORTFALL * (target - 1) * CORRECTION_TIME / (target * target) : 0;
}

static float correct_speed(speechwarp_stream* s, float speed) {
  double target = s->speed;
  double gain = target * target / CORRECTION_TIME;
  double corrected = speed;

  if (s->correction) {
    corrected += gain * s->excess;
  }
  if (corrected < SONIC_MIN_SPEED) corrected = SONIC_MIN_SPEED;
  if (corrected > SONIC_MAX_SPEED) corrected = SONIC_MAX_SPEED;

  s->excess += (double)s->block / s->sample_rate * (1.0 / corrected - 1.0 / target);
  if (s->excess < 0) s->excess = 0;
  if (s->excess > CORRECTION_TIME / target) s->excess = CORRECTION_TIME / target; /* at most doubles the speed */
  return (float)corrected;
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
      if (!release(s, s->block, s->speed)) {
        return 0;
      }
    }
  }
}

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
  s->hold_offset = 0;
  s->hold_frames = 0;
  s->blocks_analysed = 0;
  s->blocks_released = 0;
  start_correction(s);
  s->frames_written = 0;
  s->frames_released = 0;
  s->frames_read = 0;
  s->marks_head = 0;
  s->marks_count = 1;
  s->marks[0].in = 0;
  s->marks[0].out = 0;
}

const char* speechwarp_version(void) { return SPEECHWARP_VERSION; }

speechwarp_stream* speechwarp_create(int sample_rate, int channels) {
  speechwarp_stream* s;

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
  if (!create_engines(s)) {
    free(s);
    return NULL;
  }
  s->block = speedyInputFrameStep(s->speedy);
  s->window = speedyInputFrameSize(s->speedy);
  s->hold_capacity = HOLD_BLOCKS * s->block;
  s->marks_capacity = 64;
  s->hold = (int16_t*)malloc((size_t)s->hold_capacity * channels * sizeof(int16_t));
  s->mono = (int16_t*)malloc((size_t)s->window * sizeof(int16_t));
  s->marks = (mark*)malloc((size_t)s->marks_capacity * sizeof(mark));
  if (!s->hold || !s->mono || !s->marks) {
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
    }
  }
}

float speechwarp_get_nonlinear(const speechwarp_stream* stream) { return stream ? stream->nonlinear : 0; }

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
  while (frames > 0) {
    int room = s->hold_capacity - s->hold_offset - s->hold_frames;
    int take;
    size_t count;
    int16_t* to;

    if (room == 0) {
      memmove(s->hold, s->hold + (size_t)s->hold_offset * s->channels,
              (size_t)s->hold_frames * s->channels * sizeof(int16_t));
      s->hold_offset = 0;
      room = s->hold_capacity - s->hold_frames;
    }
    take = frames < room ? frames : room;
    count = (size_t)take * s->channels;
    to = s->hold + ((size_t)s->hold_offset + s->hold_frames) * s->channels;
    if (ints) {
      memcpy(to, ints, count * sizeof(int16_t));
      ints += count;
    } else {
      size_t i;
      for (i = 0; i < count; i++) {
        to[i] = float_to_i16(floats[i]);
      }
      floats += count;
    }
    s->hold_frames += take;
    s->frames_written += take;
    frames -= take;
    if (!pump(s)) {
      return 0;
    }
  }
  return 1;
}

int speechwarp_write(speechwarp_stream* stream, const float* samples, int frames) {
  return write_frames(stream, samples, NULL, frames);
}

int speechwarp_write_i16(speechwarp_stream* stream, const int16_t* samples, int frames) {
  return write_frames(stream, NULL, samples, frames);
}

static int finish_read(speechwarp_stream* s, int frames) {
  s->frames_read += frames;
  while (s->marks_head + 1 < s->marks_count && s->marks[s->marks_head + 1].out <= s->frames_read) {
    s->marks_head++;
  }
  return frames;
}

int speechwarp_read(speechwarp_stream* stream, float* samples, int max_frames) {
  if (!stream || !stream->sonic || !samples || max_frames <= 0) {
    return 0;
  }
  return finish_read(stream, sonicReadFloatFromStream(stream->sonic, samples, max_frames));
}

int speechwarp_read_i16(speechwarp_stream* stream, int16_t* samples, int max_frames) {
  if (!stream || !stream->sonic || !samples || max_frames <= 0) {
    return 0;
  }
  return finish_read(stream, sonicReadShortFromStream(stream->sonic, samples, max_frames));
}

int speechwarp_available(const speechwarp_stream* stream) {
  return stream && stream->sonic ? sonicSamplesAvailable(stream->sonic) : 0;
}

int speechwarp_flush(speechwarp_stream* stream) {
  speechwarp_stream* s = stream;
  mark* last;

  if (!s || !s->sonic) {
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
  return 1;
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
         (after->in - before->in) * (stream->frames_read - before->out) / (after->out - before->out);
}

void speechwarp_priv_set_speed_correction(speechwarp_stream* stream, int enabled) {
  stream->correction = enabled;
  start_correction(stream);
}
