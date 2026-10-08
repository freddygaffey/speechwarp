/* Tests of the options for very high speeds: pause cap, keep overall speed, speed floor, rhythm, and the
 * syllable counter. That they change nothing while off is checked by test_parity. */
#include <string.h>

#include "speechwarp.h"
#include "test_util.h"

#define RATE 44100
#define PI 3.141592653589793

typedef struct {
  float pause_cap;
  int keep_speed;
  float floor;
  float gap;
  float gap_rate;
} options;

static const options all_off = {0, 1, 0, 0, 5};

typedef struct {
  float* samples;
  int frames;
  int capacity;
  int position_ok; /* position never went backwards or past the input */
  int64_t final_position;
} recording;

static void take_output(speechwarp_stream* s, recording* out, int64_t* last_position, int64_t written) {
  for (;;) {
    int n;
    int64_t position;
    if (out->capacity - out->frames < 4096) {
      out->capacity = out->capacity * 2 + 8192;
      out->samples = (float*)realloc(out->samples, (size_t)out->capacity * sizeof(float));
    }
    n = speechwarp_read(s, out->samples + out->frames, 1 + (int)(random_unit() * 3000));
    position = speechwarp_position(s);
    if (position < *last_position || position > written) out->position_ok = 0;
    *last_position = position;
    if (n == 0) return;
    out->frames += n;
  }
}

static void apply(speechwarp_stream* s, const options* o) {
  speechwarp_set_pause_cap(s, o->pause_cap);
  speechwarp_set_keep_speed(s, o->keep_speed);
  speechwarp_set_speed_floor(s, o->floor);
  speechwarp_set_rhythm_gap(s, o->gap);
  speechwarp_set_rhythm_rate(s, o->gap_rate);
}

/* Run mono float input through a stream in pieces of `chunk` frames (0: random sizes). */
static recording process(const float* in, int frames, float speed, float nonlinear, const options* o,
                         int chunk) {
  recording out = {NULL, 0, 0, 1, 0};
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  int64_t last_position = 0;
  int position = 0;

  speechwarp_set_speed(s, speed);
  speechwarp_set_nonlinear(s, nonlinear);
  apply(s, o);
  random_state = 99;
  while (position < frames) {
    int n = chunk ? chunk : 1 + (int)(random_unit() * 5000);
    if (n > frames - position) n = frames - position;
    CHECK(speechwarp_write(s, in + position, n));
    position += n;
    take_output(s, &out, &last_position, position);
  }
  CHECK(speechwarp_flush(s));
  take_output(s, &out, &last_position, frames);
  out.final_position = speechwarp_position(s);
  CHECK(speechwarp_available(s) == 0);
  speechwarp_destroy(s);
  return out;
}

static float* to_float(const int16_t* in, int frames) {
  float* out = (float*)malloc((size_t)frames * sizeof(float));
  int i;
  for (i = 0; i < frames; i++) out[i] = in[i] / 32768.0f;
  return out;
}

static float* speech(double seconds, uint32_t seed, int* frames) {
  int16_t* ints = make_speech(RATE, seconds, seed, frames);
  float* floats = to_float(ints, *frames);
  free(ints);
  return floats;
}

/* A vowel-like sound: a 120 Hz voice with harmonics. */
static double voice(double t) {
  double v = 0;
  int h;
  for (h = 1; h <= 10; h++) v += sin(2 * PI * 120 * h * t) / h;
  return 0.2 * v;
}

/* `count` bursts of voice `on` seconds long, separated by `off` seconds of `floor`-level 50 Hz hum. */
static float* bursts(int count, double on, double off, double floor, int* frames) {
  int total = (int)(RATE * (count * on + (count - 1) * off));
  float* out = (float*)malloc((size_t)total * sizeof(float));
  int i;
  for (i = 0; i < total; i++) {
    double t = (double)i / RATE;
    double within = fmod(t, on + off);
    out[i] = (float)(within < on ? voice(t) : floor * sin(2 * PI * 50 * t));
  }
  *frames = total;
  return out;
}

static double largest_step(const recording* r) {
  double most = 0;
  int i;
  for (i = 1; i < r->frames; i++) {
    double step = fabs(r->samples[i] - r->samples[i - 1]);
    if (step > most) most = step;
  }
  return most;
}

static void test_settings(void) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  float nan = (float)NAN;

  CHECK(speechwarp_get_pause_cap(s) == 0);
  CHECK(speechwarp_get_keep_speed(s) == 1);
  CHECK(speechwarp_get_speed_floor(s) == 0);
  CHECK(speechwarp_get_rhythm_gap(s) == 0);
  CHECK(speechwarp_get_rhythm_rate(s) == 5);

  speechwarp_set_pause_cap(s, 0.06f);
  CHECK(fabs(speechwarp_get_pause_cap(s) - 0.06f) < 1e-6);
  speechwarp_set_pause_cap(s, nan);
  CHECK(fabs(speechwarp_get_pause_cap(s) - 0.06f) < 1e-6);
  speechwarp_set_pause_cap(s, 0.001f);
  CHECK(fabs(speechwarp_get_pause_cap(s) - 0.01f) < 1e-6);
  speechwarp_set_pause_cap(s, 50);
  CHECK(speechwarp_get_pause_cap(s) == 1);
  speechwarp_set_pause_cap(s, -1);
  CHECK(speechwarp_get_pause_cap(s) == 0);

  speechwarp_set_keep_speed(s, 0);
  CHECK(speechwarp_get_keep_speed(s) == 0);
  speechwarp_set_keep_speed(s, 7);
  CHECK(speechwarp_get_keep_speed(s) == 1);

  speechwarp_set_speed_floor(s, 0.5f);
  CHECK(speechwarp_get_speed_floor(s) == 0.5f);
  speechwarp_set_speed_floor(s, nan);
  CHECK(speechwarp_get_speed_floor(s) == 0.5f);
  speechwarp_set_speed_floor(s, 3);
  CHECK(speechwarp_get_speed_floor(s) == 1);
  speechwarp_set_speed_floor(s, -3);
  CHECK(speechwarp_get_speed_floor(s) == 0);

  speechwarp_set_rhythm_gap(s, 0.04f);
  CHECK(fabs(speechwarp_get_rhythm_gap(s) - 0.04f) < 1e-6);
  speechwarp_set_rhythm_gap(s, 1);
  CHECK(fabs(speechwarp_get_rhythm_gap(s) - 0.2f) < 1e-6);
  speechwarp_set_rhythm_gap(s, 0.0001f);
  CHECK(fabs(speechwarp_get_rhythm_gap(s) - 0.005f) < 1e-6);
  speechwarp_set_rhythm_gap(s, 0);
  CHECK(speechwarp_get_rhythm_gap(s) == 0);

  speechwarp_set_rhythm_rate(s, 8);
  CHECK(speechwarp_get_rhythm_rate(s) == 8);
  speechwarp_set_rhythm_rate(s, 0);
  speechwarp_set_rhythm_rate(s, -2);
  speechwarp_set_rhythm_rate(s, nan);
  CHECK(speechwarp_get_rhythm_rate(s) == 8);
  speechwarp_set_rhythm_rate(s, 100);
  CHECK(speechwarp_get_rhythm_rate(s) == 16);
  speechwarp_set_rhythm_rate(s, 0.1f);
  CHECK(speechwarp_get_rhythm_rate(s) == 1);

  CHECK(speechwarp_syllable_rate(s) < 0);
  speechwarp_destroy(s);

  /* NULL streams are tolerated. */
  speechwarp_set_pause_cap(NULL, 1);
  speechwarp_set_keep_speed(NULL, 1);
  speechwarp_set_speed_floor(NULL, 1);
  speechwarp_set_rhythm_gap(NULL, 1);
  speechwarp_set_rhythm_rate(NULL, 1);
  CHECK(speechwarp_get_pause_cap(NULL) == 0);
  CHECK(speechwarp_get_keep_speed(NULL) == 0);
  CHECK(speechwarp_get_speed_floor(NULL) == 0);
  CHECK(speechwarp_get_rhythm_gap(NULL) == 0);
  CHECK(speechwarp_get_rhythm_rate(NULL) == 0);
  CHECK(speechwarp_syllable_rate(NULL) < 0);
}

/* Each pause comes out at the cap, give or take a block. */
static void test_pause_cap(void) {
  const int count = 10;
  const double on = 0.4, off = 0.8, cap = 0.06;
  double floors[2] = {0, 0.003}; /* digital silence, and hum 36 dB below the voice */
  int f, nonlinear;

  for (f = 0; f < 2; f++) {
    int frames;
    float* in = bursts(count, on, off, floors[f], &frames);
    for (nonlinear = 0; nonlinear <= 1; nonlinear++) {
      options o = all_off;
      recording plain, capped;
      double expected = frames - (count - 1) * (off - cap) * RATE;
      o.pause_cap = (float)cap;
      plain = process(in, frames, 1, (float)nonlinear, &all_off, 4410);
      capped = process(in, frames, 1, (float)nonlinear, &o, 4410);
      if (fabs(capped.frames - expected) > (count - 1) * 0.015 * RATE) {
        printf("pause cap, floor %g, nonlinear %d: %d frames, expected %.0f\n", floors[f], nonlinear,
               capped.frames, expected);
      }
      CHECK(abs(plain.frames - frames) < RATE / 50);
      CHECK(fabs(capped.frames - expected) <= (count - 1) * 0.015 * RATE);
      CHECK(capped.position_ok);
      CHECK(capped.final_position == frames);
      free(plain.samples);
      free(capped.samples);
    }
    free(in);
  }
}

/* The cut in a pause is crossfaded: hum that jumps phase where pauses are trimmed would otherwise click. */
static void test_pause_cap_does_not_click(void) {
  int frames, i;
  float* in = (float*)malloc((size_t)RATE * 4 * sizeof(float));
  options o = all_off;
  recording out;

  frames = RATE * 4;
  for (i = 0; i < frames; i++) {
    double t = (double)i / RATE;
    double hum = 0.012 * sin(2 * PI * 50 * t);
    in[i] = (float)(t < 1 || t >= 3 ? 0.5 * sin(2 * PI * 100 * t) : hum);
  }
  o.pause_cap = 0.07f;
  out = process(in, frames, 1, 0, &o, 0);
  CHECK(out.frames < frames - RATE);  /* most of the pause went */
  CHECK(largest_step(&out) < 0.01);   /* the 100 Hz tone steps by at most 0.0071 */
  free(out.samples);
  free(in);
}

/* With keep speed on, the overall speed stays at the one set even though pauses are trimmed and gaps put in;
 * with it off, trimmed pauses make it faster. */
static void test_keep_speed(void) {
  static const float speeds[] = {3, 6};
  int frames, k, nonlinear;
  float* in = speech(60, 5, &frames);

  for (k = 0; k < 2; k++) {
    for (nonlinear = 0; nonlinear <= 1; nonlinear++) {
      options o = all_off;
      recording kept, capped, both;
      double target = frames / speeds[k];

      o.pause_cap = 0.06f;
      kept = process(in, frames, speeds[k], (float)nonlinear, &o, 0);
      o.gap = 0.04f;
      o.gap_rate = 6;
      o.floor = nonlinear ? 0.5f : 0;
      both = process(in, frames, speeds[k], (float)nonlinear, &o, 0);
      o = all_off;
      o.pause_cap = 0.06f;
      o.keep_speed = 0;
      capped = process(in, frames, speeds[k], (float)nonlinear, &o, 0);

      printf("speed %g nonlinear %d: kept %.3f, with rhythm and floor %.3f, not kept %.3f of the target "
             "length\n",
             speeds[k], nonlinear, kept.frames / target, both.frames / target, capped.frames / target);
      CHECK(fabs(kept.frames / target - 1) < 0.04);
      CHECK(fabs(both.frames / target - 1) < 0.04);
      CHECK(capped.frames / target < kept.frames / target - 0.03);
      CHECK(kept.position_ok && both.position_ok && capped.position_ok);
      CHECK(kept.final_position == frames && both.final_position == frames && capped.final_position == frames);
      free(kept.samples);
      free(both.samples);
      free(capped.samples);
    }
  }
  free(in);
}

/* Position holds still in every gap, never goes backwards, and ends at the end, with every option on. */
static void test_position_with_everything_on(void) {
  int frames, nonlinear;
  float* in = speech(30, 8, &frames);
  options o = {0.06f, 1, 0.5f, 0.05f, 6};

  for (nonlinear = 0; nonlinear <= 1; nonlinear++) {
    recording out = process(in, frames, 6.5f, (float)nonlinear, &o, 0);
    CHECK(out.position_ok);
    CHECK(out.final_position == frames);
    free(out.samples);
  }

  /* Read a frame at a time through the first gaps: position must not move inside one. */
  {
    speechwarp_stream* s = speechwarp_create(RATE, 1);
    float sample;
    int zero_run = 0, held_in_gap = 1, i;
    int64_t last = 0;
    speechwarp_set_speed(s, 3);
    speechwarp_set_rhythm_gap(s, 0.05f);
    for (i = 0; i < RATE * 4; i++) {
      float x = (float)voice((double)i / RATE);
      CHECK(speechwarp_write(s, &x, 1));
      while (speechwarp_read(s, &sample, 1) == 1) {
        int64_t position = speechwarp_position(s);
        zero_run = sample == 0 ? zero_run + 1 : 0;
        if (zero_run > 3 && position != last) held_in_gap = 0;
        last = position;
      }
    }
    CHECK(held_in_gap);
    speechwarp_destroy(s);
  }
  free(in);
}

/* Gaps fade out and in: the output of a steady tone never steps much further than the tone does. */
static void test_gaps_do_not_click(void) {
  int frames = RATE * 6, i;
  float* in = (float*)malloc((size_t)frames * sizeof(float));
  options o = all_off;
  int nonlinear;

  for (i = 0; i < frames; i++) in[i] = (float)(0.5 * sin(2 * PI * 220 * i / RATE));
  for (nonlinear = 0; nonlinear <= 1; nonlinear++) {
    recording plain, gapped;
    int gaps = 0, run = 0;
    o.gap = 0.04f;
    o.gap_rate = 6;
    plain = process(in, frames, 3, (float)nonlinear, &all_off, 0);
    gapped = process(in, frames, 3, (float)nonlinear, &o, 0);
    for (i = 0; i < gapped.frames; i++) {
      run = gapped.samples[i] == 0 ? run + 1 : 0;
      if (run == (int)(0.03 * RATE)) gaps++;
    }
    if (largest_step(&gapped) > largest_step(&plain) * 1.2 + 0.005) {
      printf("gaps, nonlinear %d: largest step %g, without gaps %g\n", nonlinear, largest_step(&gapped),
             largest_step(&plain));
    }
    CHECK(largest_step(&gapped) <= largest_step(&plain) * 1.2 + 0.005);
    CHECK(gaps >= 6 * 2 * 0.8 && gaps <= 6 * 2 * 1.2); /* six a second over two seconds of output */
    free(plain.samples);
    free(gapped.samples);
  }
  free(in);
}

/* With everything on, the output still does not depend on how the input is divided. */
static void test_chunking(void) {
  int frames;
  float* in = speech(10, 3, &frames);
  options o = {0.06f, 1, 0.5f, 0.04f, 6};
  recording a = process(in, frames, 5, 1, &o, 1000);
  recording b = process(in, frames, 5, 1, &o, 0);
  CHECK(a.frames == b.frames && memcmp(a.samples, b.samples, (size_t)a.frames * sizeof(float)) == 0);
  free(a.samples);
  free(b.samples);
  free(in);
}

/* The syllable counter on synthetic syllables: vowel bursts swelling and fading `per_second` times a second. */
static double syllable_rate(double per_second, double seconds, int channels, int noise) {
  speechwarp_stream* s = speechwarp_create(RATE, channels);
  int frames = (int)(RATE * seconds), i, c, position;
  float* in = (float*)malloc((size_t)frames * channels * sizeof(float));
  double rate;

  random_state = 1;
  for (i = 0; i < frames; i++) {
    double t = (double)i / RATE;
    double envelope = pow(sin(PI * t * per_second), 2);
    double x = noise ? 0.2 * envelope * (random_unit() * 2 - 1) : envelope * voice(t);
    for (c = 0; c < channels; c++) in[i * channels + c] = (float)x;
  }
  for (position = 0; position < frames; position += 1000) {
    int n = frames - position < 1000 ? frames - position : 1000;
    float sink[8192];
    speechwarp_write(s, in + (size_t)position * channels, n);
    while (speechwarp_read(s, sink, 8192 / channels) > 0) {
    }
  }
  rate = speechwarp_syllable_rate(s);
  speechwarp_destroy(s);
  free(in);
  return rate;
}

static void test_syllable_rate(void) {
  static const double rates[] = {3, 5, 8};
  int k;
  for (k = 0; k < 3; k++) {
    double rate = syllable_rate(rates[k], 20, 1, 0);
    if (fabs(rate - rates[k]) > rates[k] * 0.1) printf("syllables: %g a second, counted %g\n", rates[k], rate);
    CHECK(fabs(rate - rates[k]) <= rates[k] * 0.1);
  }
  CHECK(fabs(syllable_rate(4, 15, 2, 0) - syllable_rate(4, 15, 1, 0)) < 0.01);
  CHECK(syllable_rate(4, 9, 1, 0) < 0);   /* not enough heard yet */
  CHECK(syllable_rate(4, 12, 1, 1) < 0.5); /* hiss is not syllables */

  {
    /* Silence has none, and reset forgets what came before. */
    speechwarp_stream* s = speechwarp_create(RATE, 1);
    float* silence = (float*)calloc((size_t)RATE * 12, sizeof(float));
    float* sound = (float*)malloc((size_t)RATE * 12 * sizeof(float));
    int i;
    for (i = 0; i < RATE * 12; i++) {
      sound[i] = (float)(pow(sin(PI * i * 5.0 / RATE), 2) * voice((double)i / RATE));
    }
    speechwarp_set_speed(s, 4);
    speechwarp_write(s, sound, RATE * 12);
    CHECK(speechwarp_syllable_rate(s) > 4);
    speechwarp_reset(s);
    CHECK(speechwarp_syllable_rate(s) < 0);
    speechwarp_write(s, silence, RATE * 12);
    CHECK(speechwarp_syllable_rate(s) == 0);
    speechwarp_destroy(s);
    free(silence);
    free(sound);
  }
}

/* ---- The rules that follow the speed ---- */

static int near(double a, double b) { return fabs(a - b) < 1e-5; }

static void test_heard_pause_rule(void) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  static const float speeds[] = {1, 2.9f, 3, 4, 5, 7.5f, 12, 13.4f, 20};
  static const float caps[] = {0, 0, 0.09f, 0.12f, 0.15f, 0.225f, 0.36f, 0.4f, 0.4f};
  int k;

  CHECK(speechwarp_get_heard_pause(s) == 0);
  speechwarp_set_heard_pause(s, 0.03f, 3);
  CHECK(near(speechwarp_get_heard_pause(s), 0.03));
  CHECK(near(speechwarp_get_heard_pause_from(s), 3));
  for (k = 0; k < 9; k++) {
    speechwarp_set_speed(s, speeds[k]);
    CHECK(near(speechwarp_get_pause_cap(s), caps[k]));
  }

  /* A very short heard pause is held to the 0.03 s minimum cap, and the arguments are clamped. */
  speechwarp_set_heard_pause(s, 0.002f, 1);
  speechwarp_set_speed(s, 4);
  CHECK(near(speechwarp_get_pause_cap(s), 0.03));
  speechwarp_set_heard_pause(s, 5, 99);
  CHECK(near(speechwarp_get_heard_pause(s), 0.4));
  CHECK(near(speechwarp_get_heard_pause_from(s), 20));
  speechwarp_set_speed(s, 20);
  CHECK(near(speechwarp_get_pause_cap(s), 0.4));
  speechwarp_set_heard_pause(s, 0.0001f, 0);
  CHECK(near(speechwarp_get_heard_pause(s), 0.002));
  CHECK(near(speechwarp_get_heard_pause_from(s), 1));

  /* NaN is ignored. */
  speechwarp_set_heard_pause(s, 0.03f, 3);
  speechwarp_set_heard_pause(s, NAN, 5);
  speechwarp_set_heard_pause(s, 0.1f, NAN);
  CHECK(near(speechwarp_get_heard_pause(s), 0.03));
  CHECK(near(speechwarp_get_heard_pause_from(s), 3));

  /* Zero turns the rule off, and the pause cap with it. */
  speechwarp_set_speed(s, 6);
  CHECK(speechwarp_get_pause_cap(s) > 0);
  speechwarp_set_heard_pause(s, 0, 3);
  CHECK(speechwarp_get_heard_pause(s) == 0);
  CHECK(speechwarp_get_pause_cap(s) == 0);
  speechwarp_set_speed(s, 7);
  CHECK(speechwarp_get_pause_cap(s) == 0);
  speechwarp_destroy(s);
}

static void test_floor_blend_rule(void) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  static const float speeds[] = {1, 3, 4, 5, 5.5f, 6, 8, 20};
  static const float floors[] = {0, 0, 0, 0.25f, 0.375f, 0.5f, 0.5f, 0.5f};
  int k;

  speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  CHECK(near(speechwarp_get_floor_blend(s), 0.5));
  CHECK(near(speechwarp_get_floor_blend_from(s), 4));
  CHECK(near(speechwarp_get_floor_blend_full(s), 6));
  for (k = 0; k < 8; k++) {
    speechwarp_set_speed(s, speeds[k]);
    CHECK(near(speechwarp_get_speed_floor(s), floors[k]));
  }

  /* A full speed that is not above the from speed makes a step. */
  speechwarp_set_floor_blend(s, 0.6f, 4, 4);
  CHECK(near(speechwarp_get_floor_blend_full(s), 4));
  speechwarp_set_speed(s, 3.9f);
  CHECK(speechwarp_get_speed_floor(s) == 0);
  speechwarp_set_speed(s, 4);
  CHECK(near(speechwarp_get_speed_floor(s), 0.6));
  speechwarp_set_speed(s, 9);
  CHECK(near(speechwarp_get_speed_floor(s), 0.6));
  speechwarp_set_floor_blend(s, 0.6f, 5, 2);
  CHECK(near(speechwarp_get_floor_blend_full(s), 5));

  /* Clamps and NaN. */
  speechwarp_set_floor_blend(s, 3, 0, 99);
  CHECK(near(speechwarp_get_floor_blend(s), 1));
  CHECK(near(speechwarp_get_floor_blend_from(s), 1));
  CHECK(near(speechwarp_get_floor_blend_full(s), 20));
  speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  speechwarp_set_floor_blend(s, NAN, 1, 2);
  speechwarp_set_floor_blend(s, 0.1f, NAN, 2);
  speechwarp_set_floor_blend(s, 0.1f, 1, NAN);
  CHECK(near(speechwarp_get_floor_blend(s), 0.5));
  CHECK(near(speechwarp_get_floor_blend_from(s), 4));
  CHECK(near(speechwarp_get_floor_blend_full(s), 6));

  /* Zero turns the rule off, and the floor with it. */
  speechwarp_set_speed(s, 8);
  CHECK(speechwarp_get_speed_floor(s) > 0);
  speechwarp_set_floor_blend(s, 0, 4, 6);
  CHECK(speechwarp_get_floor_blend(s) == 0);
  CHECK(speechwarp_get_speed_floor(s) == 0);
  speechwarp_destroy(s);
}

static void test_fixed_and_rules_replace_each_other(void) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);

  speechwarp_set_heard_pause(s, 0.03f, 3);
  speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  speechwarp_set_speed(s, 5);
  CHECK(near(speechwarp_get_pause_cap(s), 0.15));
  CHECK(near(speechwarp_get_speed_floor(s), 0.25));

  /* A fixed value turns its rule off and then stays put as the speed moves. */
  speechwarp_set_pause_cap(s, 0.2f);
  speechwarp_set_speed_floor(s, 0.3f);
  CHECK(speechwarp_get_heard_pause(s) == 0);
  CHECK(speechwarp_get_floor_blend(s) == 0);
  speechwarp_set_speed(s, 8);
  CHECK(near(speechwarp_get_pause_cap(s), 0.2));
  CHECK(near(speechwarp_get_speed_floor(s), 0.3));

  /* And a rule replaces the fixed value at once. */
  speechwarp_set_heard_pause(s, 0.03f, 3);
  speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  CHECK(near(speechwarp_get_pause_cap(s), 0.24));
  CHECK(near(speechwarp_get_speed_floor(s), 0.5));
  speechwarp_set_speed(s, 5);
  CHECK(near(speechwarp_get_pause_cap(s), 0.15));
  CHECK(near(speechwarp_get_speed_floor(s), 0.25));

  /* NaN to the fixed setters leaves the rules on. */
  speechwarp_set_pause_cap(s, NAN);
  speechwarp_set_speed_floor(s, NAN);
  CHECK(near(speechwarp_get_heard_pause(s), 0.03));
  CHECK(near(speechwarp_get_floor_blend(s), 0.5));

  /* Reset keeps the rules and what they gave. */
  speechwarp_reset(s);
  CHECK(near(speechwarp_get_heard_pause(s), 0.03));
  CHECK(near(speechwarp_get_heard_pause_from(s), 3));
  CHECK(near(speechwarp_get_floor_blend(s), 0.5));
  CHECK(near(speechwarp_get_floor_blend_full(s), 6));
  CHECK(near(speechwarp_get_pause_cap(s), 0.15));
  CHECK(near(speechwarp_get_speed_floor(s), 0.25));
  speechwarp_set_speed(s, 6);
  CHECK(near(speechwarp_get_pause_cap(s), 0.18));
  CHECK(near(speechwarp_get_speed_floor(s), 0.5));
  speechwarp_destroy(s);
}

static void test_ramp_is_continuous(void) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  double previous_floor = 0, previous_cap = 0;
  int k;

  speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  speechwarp_set_heard_pause(s, 0.03f, 3);
  for (k = 10; k <= 80; k++) {
    double speed = k / 10.0, floor, cap;
    speechwarp_set_speed(s, (float)speed);
    floor = speechwarp_get_speed_floor(s);
    cap = speechwarp_get_pause_cap(s);
    /* The floor climbs 0.5 over 2x: 0.025 in a step of 0.1x. */
    CHECK(floor >= previous_floor - 1e-6 && floor - previous_floor <= 0.025 + 1e-5);
    /* The cap is 0.03 a second of speed, once it is on at 3x. */
    if (k > 30) CHECK(cap >= previous_cap - 1e-6 && cap - previous_cap <= 0.003 + 1e-5);
    previous_floor = floor;
    previous_cap = cap;
  }
  speechwarp_destroy(s);
}

/* Write all of `in` through a stream set up by `setup`, in odd-sized pieces, and return a fingerprint. */
static double fingerprint(const float* in, int frames, int heard, int* out_frames) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  float buffer[4096];
  double sum = 0;
  int position = 0, total = 0, n;

  speechwarp_set_speed(s, 5);
  if (heard) {
    speechwarp_set_heard_pause(s, 0.03f, 3);
    speechwarp_set_floor_blend(s, 0.5f, 4, 6);
  } else {
    speechwarp_set_pause_cap(s, 0.15f);
    speechwarp_set_speed_floor(s, 0.25f);
  }
  while (position < frames) {
    int piece = frames - position < 1237 ? frames - position : 1237;
    speechwarp_write(s, in + position, piece);
    position += piece;
    while ((n = speechwarp_read(s, buffer, 4096)) > 0) {
      int i;
      for (i = 0; i < n; i++) sum += buffer[i] * (double)((total + i) % 1013 + 1);
      total += n;
    }
  }
  speechwarp_flush(s);
  while ((n = speechwarp_read(s, buffer, 4096)) > 0) {
    int i;
    for (i = 0; i < n; i++) sum += buffer[i] * (double)((total + i) % 1013 + 1);
    total += n;
  }
  speechwarp_destroy(s);
  *out_frames = total;
  return sum;
}

static void test_rule_acts_like_the_fixed_value(void) {
  int frames, a, b;
  float* in = speech(20, 7, &frames);
  double fixed = fingerprint(in, frames, 0, &a);
  double rule = fingerprint(in, frames, 1, &b);
  CHECK(a == b);
  CHECK(fixed == rule);
  free(in);
}

/* ---- The standalone syllable counter ---- */

/* Syllables `per_second` a second, interleaved over `channels`, for `sound_seconds` and then silence. */
static float* syllable_signal(double per_second, double seconds, double sound_seconds, int channels) {
  int frames = (int)(RATE * seconds), i, c;
  float* in = (float*)malloc((size_t)frames * channels * sizeof(float));
  for (i = 0; i < frames; i++) {
    double t = (double)i / RATE;
    double x = t < sound_seconds ? pow(sin(PI * t * per_second), 2) * voice(t) : 0;
    for (c = 0; c < channels; c++) in[(size_t)i * channels + c] = (float)(x * (c + 1) / channels);
  }
  return in;
}

/* 16-bit samples the way the library converts float ones. */
static int16_t* to_16(const float* in, int count) {
  int16_t* ints = (int16_t*)malloc((size_t)count * sizeof(int16_t));
  int i;
  for (i = 0; i < count; i++) ints[i] = (int16_t)lrintf(in[i] * 32767.0f);
  return ints;
}

static void test_syllables_standalone(void) {
  int channels, as_ints;

  for (channels = 1; channels <= 2; channels++) {
    for (as_ints = 0; as_ints <= 1; as_ints++) {
      int frames = RATE * 25, position;
      float* in = syllable_signal(5, 25, 25, channels);
      int16_t* ints;
      speechwarp_stream* s = speechwarp_create(RATE, channels);
      speechwarp_syllables* c = speechwarp_syllables_create(RATE, channels);
      float sink[8192];
      double from_stream, from_counter;

      ints = to_16(in, frames * channels);
      for (position = 0; position < frames; position += 1000) {
        int n = frames - position < 1000 ? frames - position : 1000;
        if (as_ints) speechwarp_write_i16(s, ints + (size_t)position * channels, n);
        else speechwarp_write(s, in + (size_t)position * channels, n);
        while (speechwarp_read(s, sink, 8192 / channels) > 0) {
        }
      }
      /* The counter gets different pieces, including ones that are not a whole number of 10 ms frames. */
      for (position = 0; position < frames;) {
        int n = 1 + (int)(random_unit() * 4000);
        if (n > frames - position) n = frames - position;
        if (as_ints) CHECK(speechwarp_syllables_write_i16(c, ints + (size_t)position * channels, n) == 1);
        else CHECK(speechwarp_syllables_write(c, in + (size_t)position * channels, n) == 1);
        position += n;
      }
      from_stream = speechwarp_syllable_rate(s);
      from_counter = speechwarp_syllables_rate(c, 60, 10);
      if (from_stream != from_counter) printf("syllables: stream %.12g, counter %.12g\n", from_stream, from_counter);
      CHECK(from_stream == from_counter);
      CHECK(fabs(from_counter - 5) < 0.5);
      speechwarp_syllables_destroy(c);
      speechwarp_destroy(s);
      free(ints);
      free(in);
    }
  }
}

static void test_syllables_window_and_minimum(void) {
  /* Ten seconds of syllables at 5 a second, then ten of silence. */
  float* in = syllable_signal(5, 20, 10, 1);
  speechwarp_syllables* c = speechwarp_syllables_create(RATE, 1);

  speechwarp_syllables_write(c, in, RATE * 20);
  CHECK(speechwarp_syllables_rate(c, 5, 0) == 0);
  CHECK(fabs(speechwarp_syllables_rate(c, 20, 0) - 2.5) < 0.4);
  CHECK(fabs(speechwarp_syllables_rate(c, 60, 10) - 2.5) < 0.4);
  CHECK(speechwarp_syllables_rate(c, 60, 25) < 0);   /* not enough yet */
  CHECK(speechwarp_syllables_rate(c, 60, 20) >= 0);  /* exactly enough */
  /* The minimum is held to the window: 5 s is available, 100 asked for. */
  CHECK(speechwarp_syllables_rate(c, 5, 100) == 0);
  /* The window is held to 1 to 120 s. */
  CHECK(speechwarp_syllables_rate(c, 0, 0) == speechwarp_syllables_rate(c, 1, 0));
  CHECK(speechwarp_syllables_rate(c, -3, 0) == speechwarp_syllables_rate(c, 1, 0));
  CHECK(speechwarp_syllables_rate(c, 1000, 0) == speechwarp_syllables_rate(c, 120, 0));
  CHECK(speechwarp_syllables_rate(c, 60, -4) == speechwarp_syllables_rate(c, 60, 0));
  CHECK(speechwarp_syllables_rate(c, 1000, 0) == speechwarp_syllables_rate(c, 20, 0));

  /* Reset forgets everything, and the counter then gives what a new one gives. */
  speechwarp_syllables_reset(c);
  CHECK(speechwarp_syllables_rate(c, 60, 10) < 0);
  CHECK(speechwarp_syllables_rate(c, 60, 0) < 0);
  speechwarp_syllables_write(c, in, RATE * 20);
  CHECK(fabs(speechwarp_syllables_rate(c, 20, 0) - 2.5) < 0.4);
  speechwarp_syllables_destroy(c);
  free(in);
}

static void test_syllables_two_minutes(void) {
  /* The window reaches back two minutes: 8 a second for 130 s. */
  float* in = syllable_signal(8, 130, 130, 1);
  speechwarp_syllables* c = speechwarp_syllables_create(RATE, 1);
  double r120, r60;
  speechwarp_syllables_write(c, in, RATE * 130);
  r120 = speechwarp_syllables_rate(c, 120, 0);
  r60 = speechwarp_syllables_rate(c, 60, 0);
  CHECK(fabs(r120 - 8) < 0.8);
  CHECK(fabs(r60 - 8) < 0.8);
  speechwarp_syllables_destroy(c);
  free(in);
}

static void test_syllables_arguments(void) {
  float one[10] = {0};
  speechwarp_syllables* c;

  CHECK(speechwarp_syllables_create(3999, 1) == NULL);
  CHECK(speechwarp_syllables_create(384001, 1) == NULL);
  CHECK(speechwarp_syllables_create(44100, 0) == NULL);
  CHECK(speechwarp_syllables_create(44100, 33) == NULL);
  c = speechwarp_syllables_create(4000, 32);
  CHECK(c != NULL);
  CHECK(speechwarp_syllables_write(c, NULL, 5) == 0);
  CHECK(speechwarp_syllables_write(c, one, -1) == 0);
  CHECK(speechwarp_syllables_write(c, NULL, 0) == 1);
  CHECK(speechwarp_syllables_write_i16(c, NULL, 5) == 0);
  CHECK(speechwarp_syllables_write(NULL, one, 1) == 0);
  CHECK(speechwarp_syllables_rate(NULL, 60, 10) < 0);
  speechwarp_syllables_reset(NULL);
  speechwarp_syllables_destroy(NULL);
  speechwarp_syllables_destroy(c);
}

int main(void) {
  test_settings();
  test_pause_cap();
  test_pause_cap_does_not_click();
  test_keep_speed();
  test_position_with_everything_on();
  test_gaps_do_not_click();
  test_chunking();
  test_syllable_rate();
  test_heard_pause_rule();
  test_floor_blend_rule();
  test_fixed_and_rules_replace_each_other();
  test_ramp_is_continuous();
  test_rule_acts_like_the_fixed_value();
  test_syllables_standalone();
  test_syllables_window_and_minimum();
  test_syllables_two_minutes();
  test_syllables_arguments();
  if (failures) {
    printf("%d checks failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
