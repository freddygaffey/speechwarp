/* Checks the library against upstream's shim (third_party/speedy/soniclib.c), which this executable builds
 * from the untouched sources. With the average-speed correction off, and upstream's duration feedback off to
 * match, the two must produce the same samples.
 *
 * Upstream's flush drops the last partial 10 ms block, so the input here is a whole number of blocks. */
#include <string.h>

#include "internal.h"
#include "sonic2.h"
#include "speechwarp.h"
#include "test_util.h"

#define RATE 22050
#define BLOCK (RATE / 100)

static int run_upstream(const int16_t* in, int frames, int channels, float speed, int16_t* out, int capacity) {
  sonicStream stream = sonicCreateStream(RATE, channels);
  int total = 0, position, n;

  sonicSetSpeed(stream, speed);
  sonicEnableNonlinearSpeedup(stream, 1.0f);
  sonicSetDurationFeedbackStrength(stream, 0.0f);
  for (position = 0; position < frames; position += 1000) {
    int chunk = frames - position < 1000 ? frames - position : 1000;
    sonicWriteShortToStream(stream, in + (size_t)position * channels, chunk);
    while ((n = sonicReadShortFromStream(stream, out + (size_t)total * channels, capacity - total)) > 0) {
      total += n;
    }
  }
  sonicFlushStream(stream);
  while ((n = sonicReadShortFromStream(stream, out + (size_t)total * channels, capacity - total)) > 0) {
    total += n;
  }
  sonicDestroyStream(stream);
  return total;
}

static int run_speechwarp(const int16_t* in, int frames, int channels, float speed, int16_t* out,
                          int capacity) {
  speechwarp_stream* stream = speechwarp_create(RATE, channels);
  int total = 0, n;

  speechwarp_priv_set_speed_correction(stream, 0);
  speechwarp_set_speed(stream, speed);
  CHECK(speechwarp_write_i16(stream, in, frames));
  CHECK(speechwarp_flush(stream));
  while ((n = speechwarp_read_i16(stream, out + (size_t)total * channels, capacity - total)) > 0) {
    total += n;
  }
  speechwarp_destroy(stream);
  return total;
}

int main(void) {
  static const float speeds[] = {0.7f, 1.0f, 1.6f, 3.0f, 8.0f};
  int frames, channels;
  int16_t* mono = make_speech(RATE, 20, 2, &frames);
  int16_t* stereo;
  int16_t* expected;
  int16_t* actual;
  int capacity, i;
  size_t k;

  frames -= frames % BLOCK;
  capacity = 2 * frames;
  stereo = (int16_t*)malloc((size_t)frames * 2 * sizeof(int16_t));
  expected = (int16_t*)malloc((size_t)capacity * 2 * sizeof(int16_t));
  actual = (int16_t*)malloc((size_t)capacity * 2 * sizeof(int16_t));
  for (i = 0; i < frames; i++) {
    stereo[2 * i] = mono[i];
    stereo[2 * i + 1] = mono[(i + 5000) % frames]; /* a different signal on the right */
  }

  for (channels = 1; channels <= 2; channels++) {
    const int16_t* in = channels == 1 ? mono : stereo;
    for (k = 0; k < sizeof speeds / sizeof speeds[0]; k++) {
      int expected_frames = run_upstream(in, frames, channels, speeds[k], expected, capacity);
      int actual_frames = run_speechwarp(in, frames, channels, speeds[k], actual, capacity);
      int identical = expected_frames == actual_frames &&
                      memcmp(expected, actual, (size_t)actual_frames * channels * sizeof(int16_t)) == 0;
      if (!identical) {
        int first = 0;
        while (first < expected_frames * channels && first < actual_frames * channels &&
               expected[first] == actual[first]) {
          first++;
        }
        printf("%d channels, speed %g: upstream %d frames, speechwarp %d frames, first difference at %d\n",
               channels, speeds[k], expected_frames, actual_frames, first / channels);
      }
      CHECK(expected_frames > 0);
      CHECK(identical);
    }
  }

  free(mono);
  free(stereo);
  free(expected);
  free(actual);
  if (failures) {
    printf("%d checks failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
