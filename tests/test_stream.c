/* Tests of the public API. */
#include <string.h>

#include "internal.h"
#include "speechwarp.h"
#include "test_util.h"

#define RATE 44100

typedef struct {
  int16_t* samples;
  int frames;
  int capacity;
} recording;

static void take_output(speechwarp_stream* s, int channels, recording* out) {
  for (;;) {
    int n;
    if (out->capacity - out->frames < 4096) {
      out->capacity = out->capacity * 2 + 8192;
      out->samples = (int16_t*)realloc(out->samples, (size_t)out->capacity * channels * sizeof(int16_t));
    }
    n = speechwarp_read_i16(s, out->samples + (size_t)out->frames * channels, out->capacity - out->frames);
    if (n == 0) return;
    out->frames += n;
  }
}

/* Run `in` through a stream, `chunk` frames at a time. */
static recording process(const int16_t* in, int frames, int channels, float speed, float nonlinear, int chunk) {
  recording out = {NULL, 0, 0};
  speechwarp_stream* s = speechwarp_create(RATE, channels);
  int position;

  speechwarp_set_speed(s, speed);
  speechwarp_set_nonlinear(s, nonlinear);
  for (position = 0; position < frames; position += chunk) {
    int n = frames - position < chunk ? frames - position : chunk;
    CHECK(speechwarp_write_i16(s, in + (size_t)position * channels, n));
    take_output(s, channels, &out);
  }
  CHECK(speechwarp_flush(s));
  take_output(s, channels, &out);
  speechwarp_destroy(s);
  return out;
}

static int same(const recording* a, const recording* b, int channels) {
  return a->frames == b->frames &&
         memcmp(a->samples, b->samples, (size_t)a->frames * channels * sizeof(int16_t)) == 0;
}

static void test_version(void) {
  char expected[32];
  snprintf(expected, sizeof expected, "%d.%d.%d", SPEECHWARP_VERSION_MAJOR, SPEECHWARP_VERSION_MINOR,
           SPEECHWARP_VERSION_PATCH);
  CHECK(strcmp(speechwarp_version(), expected) == 0);
  CHECK(strcmp(SPEECHWARP_VERSION, expected) == 0);
}

static void test_arguments(void) {
  speechwarp_stream* s;
  float one[2] = {0, 0};

  CHECK(speechwarp_create(0, 1) == NULL);
  CHECK(speechwarp_create(-44100, 1) == NULL);
  CHECK(speechwarp_create(1000000, 1) == NULL);
  CHECK(speechwarp_create(RATE, 0) == NULL);
  CHECK(speechwarp_create(RATE, 33) == NULL);

  /* NULL streams are tolerated everywhere. */
  speechwarp_destroy(NULL);
  speechwarp_set_speed(NULL, 2);
  speechwarp_set_nonlinear(NULL, 1);
  speechwarp_reset(NULL);
  CHECK(speechwarp_get_speed(NULL) == 0);
  CHECK(speechwarp_write(NULL, one, 1) == 0);
  CHECK(speechwarp_read(NULL, one, 1) == 0);
  CHECK(speechwarp_available(NULL) == 0);
  CHECK(speechwarp_flush(NULL) == 0);
  CHECK(speechwarp_position(NULL) == 0);

  s = speechwarp_create(RATE, 2);
  CHECK(s != NULL);
  CHECK(speechwarp_get_speed(s) == 1.0f);
  CHECK(speechwarp_get_nonlinear(s) == 1.0f);

  speechwarp_set_speed(s, 3);
  speechwarp_set_speed(s, 0);
  speechwarp_set_speed(s, -2);
  speechwarp_set_speed(s, (float)NAN);
  CHECK(speechwarp_get_speed(s) == 3.0f);
  speechwarp_set_speed(s, 1000);
  CHECK(speechwarp_get_speed(s) == SPEECHWARP_MAX_SPEED);
  speechwarp_set_speed(s, 0.0001f);
  CHECK(speechwarp_get_speed(s) == SPEECHWARP_MIN_SPEED);

  speechwarp_set_nonlinear(s, 7);
  CHECK(speechwarp_get_nonlinear(s) == 1.0f);
  speechwarp_set_nonlinear(s, -1);
  CHECK(speechwarp_get_nonlinear(s) == 0.0f);

  CHECK(speechwarp_write(s, one, 0) == 1);
  CHECK(speechwarp_write(s, NULL, 1) == 0);
  CHECK(speechwarp_read(s, one, 0) == 0);
  CHECK(speechwarp_read(s, NULL, 10) == 0);
  CHECK(speechwarp_position(s) == 0);
  speechwarp_destroy(s);
}

/* At speed 1 nothing is changed, whichever way the speed would have been chosen. */
static void test_speed_one_passes_through(const int16_t* speech, int frames) {
  float nonlinear;
  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    recording in = {(int16_t*)speech, frames, frames};
    recording out = process(speech, frames, 1, 1.0f, nonlinear, 1000);
    CHECK(same(&in, &out, 1));
    free(out.samples);
  }
}

static void test_length(const int16_t* speech, int frames) {
  static const float speeds[] = {0.5f, 0.8f, 1.5f, 2, 3, 4.5f, 6, 8, 10};
  size_t i;
  float nonlinear;

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 0.5f) {
    for (i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
      recording out = process(speech, frames, 1, speeds[i], nonlinear, 4096);
      double actual = (double)frames / out.frames;
      double error = actual / speeds[i] - 1;
      /* Sonic runs up to 2% fast at high speeds. Nonlinear is steered towards the speed, and the steering
       * only speeds up, so it overshoots a little. */
      double low = nonlinear == 0 ? -0.025 : -0.03;
      double high = nonlinear == 0 ? 0.025 : 0.08;
      if (error < low || error > high) {
        printf("speed %g nonlinear %g: actual speed %.3f (%+.1f%%)\n", speeds[i], nonlinear, actual,
               100 * error);
      }
      CHECK(error >= low && error <= high);
      free(out.samples);
    }
  }
}

/* Nonlinear speed-up must actually vary the speed: the same input gives different audio. */
static void test_nonlinear_differs(const int16_t* speech, int frames) {
  recording even = process(speech, frames, 1, 3, 0, 4096);
  recording warped = process(speech, frames, 1, 3, 1, 4096);
  CHECK(!same(&even, &warped, 1));
  free(even.samples);
  free(warped.samples);
}

static void test_chunk_size_does_not_matter(const int16_t* speech, int frames) {
  static const int chunks[] = {1, 7, 441, 4096, 1 << 30};
  float nonlinear;
  size_t i;

  frames = frames < 8 * RATE ? frames : 8 * RATE;
  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    recording whole = process(speech, frames, 1, 2.7f, nonlinear, frames);
    for (i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
      recording pieces = process(speech, frames, 1, 2.7f, nonlinear, chunks[i] < frames ? chunks[i] : frames);
      CHECK(same(&whole, &pieces, 1));
      free(pieces.samples);
    }
    free(whole.samples);
  }
}

static void test_float_matches_i16(const int16_t* speech, int frames) {
  speechwarp_stream* a = speechwarp_create(RATE, 1);
  speechwarp_stream* b = speechwarp_create(RATE, 1);
  float* floats = (float*)malloc((size_t)frames * sizeof(float));
  float* from_a = (float*)malloc((size_t)frames * sizeof(float));
  int16_t* from_b = (int16_t*)malloc((size_t)frames * sizeof(int16_t));
  int i, count_a, count_b;

  for (i = 0; i < frames; i++) floats[i] = speech[i] / 32767.0f;
  speechwarp_set_speed(a, 3);
  speechwarp_set_speed(b, 3);
  CHECK(speechwarp_write(a, floats, frames));
  CHECK(speechwarp_write_i16(b, speech, frames));
  CHECK(speechwarp_flush(a));
  CHECK(speechwarp_flush(b));
  CHECK(speechwarp_available(a) == speechwarp_available(b));
  count_a = speechwarp_read(a, from_a, frames);
  count_b = speechwarp_read_i16(b, from_b, frames);
  CHECK(count_a == count_b && count_a > 0);
  for (i = 0; i < count_a && i < count_b; i++) {
    if (fabsf(from_a[i] - from_b[i] / 32767.0f) > 1e-6f) break;
  }
  CHECK(i == count_a);

  /* Out-of-range and NaN input is clipped, not wrapped around or passed on. */
  speechwarp_reset(a);
  speechwarp_set_speed(a, 1);
  for (i = 0; i < RATE; i++) floats[i] = i % 3 == 0 ? 5.0f : i % 3 == 1 ? -5.0f : (float)NAN;
  CHECK(speechwarp_write(a, floats, RATE));
  CHECK(speechwarp_flush(a));
  CHECK(speechwarp_read(a, from_a, RATE) == RATE);
  for (i = 0; i < RATE; i++) {
    float expected = i % 3 == 0 ? 1.0f : i % 3 == 1 ? -1.0f : 0.0f;
    if (from_a[i] != expected) break;
  }
  CHECK(i == RATE);

  speechwarp_destroy(a);
  speechwarp_destroy(b);
  free(floats);
  free(from_a);
  free(from_b);
}

/* Both channels carry the same audio, so both must come out the same, and the same as mono. */
static void test_stereo(const int16_t* speech, int frames) {
  int16_t* stereo;
  float nonlinear;
  int i;

  frames = frames < 10 * RATE ? frames : 10 * RATE;
  stereo = (int16_t*)malloc((size_t)frames * 2 * sizeof(int16_t));
  for (i = 0; i < frames; i++) stereo[2 * i] = stereo[2 * i + 1] = speech[i];

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    recording mono = process(speech, frames, 1, 3, nonlinear, 4096);
    recording both = process(stereo, frames, 2, 3, nonlinear, 4096);
    CHECK(mono.frames == both.frames);
    for (i = 0; i < both.frames && i < mono.frames; i++) {
      if (both.samples[2 * i] != both.samples[2 * i + 1] || both.samples[2 * i] != mono.samples[i]) break;
    }
    CHECK(i == both.frames);
    free(mono.samples);
    free(both.samples);
  }

  /* Different channels stay apart: silence on the right stays silent. */
  for (i = 0; i < frames; i++) stereo[2 * i + 1] = 0;
  {
    recording both = process(stereo, frames, 2, 3, 1, 4096);
    int loud = 0;
    for (i = 0; i < both.frames; i++) {
      if (both.samples[2 * i + 1] != 0) break;
      if (abs(both.samples[2 * i]) > 1000) loud = 1;
    }
    CHECK(i == both.frames);
    CHECK(loud);
    free(both.samples);
  }
  free(stereo);
}

static void test_reset(const int16_t* speech, int frames) {
  float nonlinear;
  frames = frames < 10 * RATE ? frames : 10 * RATE;

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    recording fresh = process(speech, frames, 1, 4, nonlinear, 4096);
    recording after = {NULL, 0, 0};
    speechwarp_stream* s = speechwarp_create(RATE, 1);
    int16_t scratch[1000];

    speechwarp_set_speed(s, 4);
    speechwarp_set_nonlinear(s, nonlinear);
    CHECK(speechwarp_write_i16(s, speech + frames / 3, frames / 2));
    CHECK(speechwarp_available(s) > 0);
    CHECK(speechwarp_read_i16(s, scratch, 1000) == 1000);
    CHECK(speechwarp_position(s) > 0);

    speechwarp_reset(s);
    CHECK(speechwarp_available(s) == 0);
    CHECK(speechwarp_position(s) == 0);
    CHECK(speechwarp_get_speed(s) == 4.0f);
    CHECK(speechwarp_get_nonlinear(s) == nonlinear);
    CHECK(speechwarp_flush(s));
    CHECK(speechwarp_available(s) == 0);

    /* Nothing of what came before is left: it now behaves like a new stream. */
    CHECK(speechwarp_write_i16(s, speech, frames));
    CHECK(speechwarp_flush(s));
    take_output(s, 1, &after);
    CHECK(same(&fresh, &after, 1));
    speechwarp_destroy(s);
    free(fresh.samples);
    free(after.samples);
  }
}

static void test_position(const int16_t* speech, int frames) {
  static const float speeds[] = {1, 2, 5, 10};
  float nonlinear;
  size_t i;

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    for (i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
      speechwarp_stream* s = speechwarp_create(RATE, 1);
      recording whole = process(speech, frames, 1, speeds[i], nonlinear, 3000);
      double achieved = (double)frames / whole.frames;
      int16_t buffer[1024];
      int64_t last = 0, read = 0, worst = 0;
      int written = 0, flushed = 0, n;

      speechwarp_set_speed(s, speeds[i]);
      speechwarp_set_nonlinear(s, nonlinear);
      /* Behave like a player: keep a little output queued and read it in small pieces. */
      for (;;) {
        int64_t position;
        while (!flushed && speechwarp_available(s) < 2048) {
          int chunk = frames - written < 3000 ? frames - written : 3000;
          if (chunk == 0) {
            CHECK(speechwarp_flush(s));
            flushed = 1;
          } else {
            CHECK(speechwarp_write_i16(s, speech + written, chunk));
            written += chunk;
          }
        }
        position = speechwarp_position(s);
        CHECK(position >= last);
        CHECK(position <= written);
        last = position;
        if (nonlinear == 0) {
          /* At an even speed the true position is known: output read so far times the speed achieved. */
          int64_t error = position - (int64_t)(read * achieved);
          if (error < 0) error = -error;
          if (error > worst) worst = error;
        }
        n = speechwarp_read_i16(s, buffer, 1024);
        if (n == 0) break;
        read += n;
      }
      CHECK(speechwarp_position(s) == frames);
      if (worst > RATE / 10) {
        printf("speed %g: position off by up to %.3f s\n", speeds[i], (double)worst / RATE);
      }
      CHECK(worst <= RATE / 10);
      speechwarp_destroy(s);
      free(whole.samples);
    }
  }
}

/* A burst of tone in the input is heard when position says it is. */
static void test_position_finds_burst(void) {
  const int frames = 20 * RATE;
  const int burst_at = 13 * RATE;
  int16_t* in = (int16_t*)calloc((size_t)frames, sizeof(int16_t));
  float nonlinear;
  int i;

  random_state = 5;
  for (i = 0; i < frames; i++) in[i] = (int16_t)(200 * (random_unit() - 0.5));
  for (i = 0; i < RATE / 20; i++) in[burst_at + i] = (int16_t)(30000 * sin(i * 0.03));

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    speechwarp_stream* s = speechwarp_create(RATE, 1);
    int16_t buffer[256];
    int64_t heard_at = -1;
    int written = 0, n;

    speechwarp_set_speed(s, 4);
    speechwarp_set_nonlinear(s, nonlinear);
    for (;;) {
      int64_t position;
      if (written < frames && speechwarp_available(s) < 256) {
        CHECK(speechwarp_write_i16(s, in + written, 2205));
        written += 2205;
        if (written == frames) CHECK(speechwarp_flush(s));
        continue;
      }
      position = speechwarp_position(s);
      n = speechwarp_read_i16(s, buffer, 256);
      if (n == 0) break;
      for (i = 0; i < n && heard_at < 0; i++) {
        if (abs(buffer[i]) > 10000) heard_at = position;
      }
    }
    CHECK(heard_at >= 0);
    if (llabs(heard_at - burst_at) > RATE / 10) {
      printf("nonlinear %g: burst at %.3f s reported at %.3f s\n", nonlinear, (double)burst_at / RATE,
             (double)heard_at / RATE);
    }
    CHECK(llabs(heard_at - burst_at) <= RATE / 10);
    speechwarp_destroy(s);
  }
  free(in);
}

static void test_speed_change(const int16_t* speech, int frames) {
  float nonlinear;
  int half = frames / 2;

  for (nonlinear = 0; nonlinear <= 1; nonlinear += 1) {
    speechwarp_stream* s = speechwarp_create(RATE, 1);
    recording out = {NULL, 0, 0};
    double expected = half / 2.0 + (frames - half) / 6.0;
    double error;

    speechwarp_set_nonlinear(s, nonlinear);
    speechwarp_set_speed(s, 2);
    CHECK(speechwarp_write_i16(s, speech, half));
    take_output(s, 1, &out);
    speechwarp_set_speed(s, 6);
    CHECK(speechwarp_write_i16(s, speech + half, frames - half));
    CHECK(speechwarp_flush(s));
    take_output(s, 1, &out);
    error = out.frames / expected - 1;
    if (fabs(error) > 0.06) printf("nonlinear %g: length off by %+.1f%%\n", nonlinear, 100 * error);
    CHECK(fabs(error) <= 0.06);
    speechwarp_destroy(s);
    free(out.samples);
  }
}

/* Switching between even and nonlinear, and flushing part way, must not drop, repeat or reorder audio. At
 * speed 1 that can be checked exactly. */
static void test_nothing_lost(const int16_t* speech, int frames) {
  speechwarp_stream* s = speechwarp_create(RATE, 1);
  recording in = {(int16_t*)speech, frames, frames};
  recording out = {NULL, 0, 0};
  int position = 0, step = 0;

  random_state = 99;
  while (position < frames) {
    int chunk = 1 + (int)(next_random() % 30000);
    if (chunk > frames - position) chunk = frames - position;
    switch (step++ % 5) {
      case 0: speechwarp_set_nonlinear(s, 0); break;
      case 1: speechwarp_set_nonlinear(s, 1); break;
      case 2: speechwarp_set_nonlinear(s, 0.5f); break;
      case 3: CHECK(speechwarp_flush(s)); break;
      default: break;
    }
    CHECK(speechwarp_write_i16(s, speech + position, chunk));
    position += chunk;
    if (step % 3 == 0) take_output(s, 1, &out);
    CHECK(speechwarp_position(s) <= position);
  }
  CHECK(speechwarp_flush(s));
  take_output(s, 1, &out);
  CHECK(same(&in, &out, 1));
  CHECK(speechwarp_position(s) == frames);
  speechwarp_destroy(s);
  free(out.samples);
}

/* Other sample rates, including ones that do not divide into 10 ms blocks. */
static void test_sample_rates(void) {
  static const int rates[] = {4000, 8000, 11025, 16000, 22050, 32000, 48000, 96000, 384000};
  size_t i;

  for (i = 0; i < sizeof rates / sizeof rates[0]; i++) {
    int frames, n, total = 0;
    int16_t* speech = make_speech(rates[i], 6, 3, &frames);
    int16_t* out = (int16_t*)malloc((size_t)frames * sizeof(int16_t));
    speechwarp_stream* s = speechwarp_create(rates[i], 1);
    double error;

    CHECK(s != NULL);
    speechwarp_set_speed(s, 3);
    CHECK(speechwarp_write_i16(s, speech, frames));
    CHECK(speechwarp_flush(s));
    while ((n = speechwarp_read_i16(s, out + total, frames - total)) > 0) total += n;
    error = (double)frames / total / 3 - 1;
    /* Loose, because six seconds is not long for the speed to settle. */
    if (fabs(error) > 0.12) printf("%d Hz: speed off by %+.1f%%\n", rates[i], 100 * error);
    CHECK(fabs(error) <= 0.12);
    CHECK(speechwarp_position(s) == frames);
    speechwarp_destroy(s);
    free(speech);
    free(out);
  }
}

int main(void) {
  int frames;
  int16_t* speech = make_speech(RATE, 40, 1, &frames);

  test_version();
  test_arguments();
  test_speed_one_passes_through(speech, frames);
  test_length(speech, frames);
  test_nonlinear_differs(speech, frames);
  test_chunk_size_does_not_matter(speech, frames);
  test_float_matches_i16(speech, 5 * RATE);
  test_stereo(speech, frames);
  test_reset(speech, frames);
  test_position(speech, frames);
  test_position_finds_burst();
  test_speed_change(speech, frames);
  test_nothing_lost(speech, frames);
  test_sample_rates();
  free(speech);

  if (failures) {
    printf("%d checks failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
