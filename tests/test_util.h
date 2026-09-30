/* Shared by the tests: a check macro and a made-up signal that behaves enough like speech. */
#ifndef SPEECHWARP_TEST_UTIL_H_
#define SPEECHWARP_TEST_UTIL_H_

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);   \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static uint32_t random_state;

static uint32_t next_random(void) {
  random_state = random_state * 1664525u + 1013904223u;
  return random_state >> 8;
}

/* A number from 0 to 1. */
static double random_unit(void) { return next_random() / 16777216.0; }

/* Mono "speech": voiced syllables with a moving pitch and a few harmonics, noise bursts for consonants, and
 * pauses. Speedy needs energy and spectral change that come and go, and Sonic needs a pitch to find. The same
 * seed always gives the same signal. */
static int16_t* make_speech(int sample_rate, double seconds, uint32_t seed, int* frames_out) {
  const double two_pi = 6.283185307179586;
  int frames = (int)(sample_rate * seconds);
  int16_t* samples = (int16_t*)malloc((size_t)frames * sizeof(int16_t));
  int position = 0;

  random_state = seed;
  while (position < frames) {
    double choice = random_unit();
    int length, i;

    if (choice < 0.6) { /* a vowel */
      double pitch = 90 + 110 * random_unit();
      double glide = (random_unit() - 0.5) * 40;
      double weights[6];
      double phase = 0;
      int h;
      for (h = 0; h < 6; h++) weights[h] = random_unit() / (h + 1);
      length = (int)(sample_rate * (0.06 + 0.16 * random_unit()));
      for (i = 0; i < length && position < frames; i++, position++) {
        double t = (double)i / length;
        double envelope = sin(3.141592653589793 * t);
        double value = 0;
        phase += two_pi * (pitch + glide * t) / sample_rate;
        for (h = 0; h < 6; h++) value += weights[h] * sin(phase * (h + 1));
        samples[position] = (int16_t)(7000 * envelope * value);
      }
    } else if (choice < 0.9) { /* a consonant */
      length = (int)(sample_rate * (0.02 + 0.06 * random_unit()));
      for (i = 0; i < length && position < frames; i++, position++) {
        samples[position] = (int16_t)(3000 * (random_unit() - 0.5));
      }
    } else { /* a pause */
      length = (int)(sample_rate * (0.1 + 0.4 * random_unit()));
      for (i = 0; i < length && position < frames; i++, position++) {
        samples[position] = 0;
      }
    }
  }
  *frames_out = frames;
  return samples;
}

#endif  /* SPEECHWARP_TEST_UTIL_H_ */
