/* How a player uses speechwarp: the audio device asks for a block of output, and the player feeds the
 * stream just enough input to supply it. This is the shape to copy for anything that plays in real time.
 *
 * It needs no audio file or sound card: the "recording" is a generated tone and the "device" is a loop. It
 * plays at 3x, seeks, changes speed and mode, and prints where in the recording it is as it goes.
 *
 *     cmake -B build && cmake --build build && build/examples/c/player
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "speechwarp.h"

#define SAMPLE_RATE 44100
#define CHANNELS 2
#define DEVICE_BLOCK 512 /* frames the device asks for at a time */
#define FEED_BLOCK 1024  /* frames given to the stream at a time */

typedef struct {
  const float* recording; /* interleaved */
  long frames;
  long next;  /* the next frame of the recording to give to the stream */
  long base;  /* the frame of the recording at which the stream was last reset */
  int ended;  /* the end of the recording has been flushed */
  speechwarp_stream* stream;
} player;

/* Fill `out` with `frames` frames. Returns how many were real; the rest are silence (the end was reached). */
static int player_render(player* p, float* out, int frames) {
  int got, i;

  /* Feed until a whole block is ready. Output lags input by a look-ahead, and at speed s each block of
   * output takes about s blocks of input, so this loop usually runs a few times. */
  while (speechwarp_available(p->stream) < frames && !p->ended) {
    long left = p->frames - p->next;
    if (left == 0) {
      speechwarp_flush(p->stream); /* no more input: let out what is held back */
      p->ended = 1;
    } else {
      int n = left < FEED_BLOCK ? (int)left : FEED_BLOCK;
      speechwarp_write(p->stream, p->recording + p->next * CHANNELS, n);
      p->next += n;
    }
  }
  got = speechwarp_read(p->stream, out, frames);
  for (i = got * CHANNELS; i < frames * CHANNELS; i++) out[i] = 0;
  return got;
}

/* The frame of the recording being heard now. Multiplying output time by the speed would be wrong: the speed
 * varies from moment to moment, and may have been changed. */
static long player_position(const player* p) { return p->base + (long)speechwarp_position(p->stream); }

static void player_seek(player* p, long frame) {
  speechwarp_reset(p->stream); /* drop what was buffered for the old place; settings are kept */
  p->base = p->next = frame;
  p->ended = 0;
}

int main(void) {
  const long frames = 60L * SAMPLE_RATE;
  float* recording = (float*)malloc((size_t)frames * CHANNELS * sizeof(float));
  float block[DEVICE_BLOCK * CHANNELS];
  player p = {0};
  long i, played = 0;
  int calls = 0;

  for (i = 0; i < frames; i++) { /* a minute of a warbling tone, in bursts */
    double t = (double)i / SAMPLE_RATE;
    float value = (float)(0.3 * sin(6.2832 * 150 * t + 3 * sin(6.2832 * 2 * t)) * (fmod(t, 0.4) < 0.3));
    recording[i * CHANNELS] = recording[i * CHANNELS + 1] = value;
  }

  p.recording = recording;
  p.frames = frames;
  p.stream = speechwarp_create(SAMPLE_RATE, CHANNELS);
  if (!p.stream) return 1;
  speechwarp_set_speed(p.stream, 3.0f);

  /* Each pass of this loop stands for one callback from the audio device. */
  for (;;) {
    int got = player_render(&p, block, DEVICE_BLOCK);
    played += got;
    calls++;

    if (calls % 200 == 0) {
      printf("after %5.2f s of listening: at %5.2f s of the recording (%gx, %s)\n",
             (double)played / SAMPLE_RATE, (double)player_position(&p) / SAMPLE_RATE,
             (double)speechwarp_get_speed(p.stream), speechwarp_get_nonlinear(p.stream) > 0 ? "nonlinear" : "even");
    }
    if (calls == 500) {
      printf("-- seek to 40 s\n");
      player_seek(&p, 40L * SAMPLE_RATE);
    }
    if (calls == 900) {
      printf("-- speed 1.5x, even\n");
      speechwarp_set_speed(p.stream, 1.5f); /* both take effect without a gap */
      speechwarp_set_nonlinear(p.stream, 0.0f);
    }
    if (got < DEVICE_BLOCK) break; /* a short block: the recording has ended */
  }
  printf("finished at %.2f s of %.2f s\n", (double)player_position(&p) / SAMPLE_RATE, (double)frames / SAMPLE_RATE);

  speechwarp_destroy(p.stream);
  free(recording);
  return 0;
}
