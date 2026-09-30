/* An FFT that is fast at every size.
 *
 * Speedy transforms 30 ms of audio, which is a number of points fixed by the sample rate. KISS FFT is only
 * fast when that number is a product of small primes. At 44.1 kHz it is 1322 = 2 x 661, and 661 is prime, so
 * KISS FFT falls back to work proportional to 661 squared and the analysis is many times slower than at
 * 48 kHz. Upstream does not notice because it normally uses FFTW.
 *
 * For such sizes this uses Bluestein's algorithm: a transform of any size can be rewritten as a convolution,
 * and a convolution can be done with two power-of-two transforms, which KISS FFT does quickly. For sizes KISS
 * FFT already handles well it is called directly and the results are bit-for-bit upstream's.
 *
 * Speedy pads its 15 ms window with as much silence again, so the second half of the input is always zero.
 * The convolution is sized for that, which halves its cost. Input that is not zero there still gets the
 * right answer, the slow way.
 */
#include "fft.h"

#include <math.h>
#include <stdlib.h>

/* KISS FFT has fast paths for 2, 3, 4 and 5 and does other prime factors p in p times the work. */
#define LARGEST_DIRECT_FACTOR 31

typedef struct {
  int size;
  int live;             /* points of input that may be non-zero on the fast path */
  int padded;           /* the power-of-two size used for the convolution, or 0 to call KISS FFT directly */
  kiss_fft_cfg direct;  /* of `size` points */
  kiss_fft_cfg forward; /* of `padded` points */
  kiss_fft_cfg inverse;
  kiss_fft_cpx* chirp;  /* exp(-i pi k^2 / size) */
  kiss_fft_cpx* filter; /* the transform of the conjugate chirp, divided by `padded` */
  kiss_fft_cpx* work;
  kiss_fft_cpx* product;
} plan;

static size_t round_up(size_t bytes) { return (bytes + 31) & ~(size_t)31; }

static int largest_prime_factor(int n) {
  int largest = 1, p;
  for (p = 2; p * p <= n; p++) {
    while (n % p == 0) {
      largest = p;
      n /= p;
    }
  }
  return n > 1 ? n : largest;
}

static size_t kiss_plan_bytes(int size) {
  size_t bytes = 0;
  kiss_fft_alloc(size, 0, NULL, &bytes);
  return round_up(bytes);
}

static kiss_fft_cpx multiply(kiss_fft_cpx a, kiss_fft_cpx b) {
  kiss_fft_cpx c;
  c.r = a.r * b.r - a.i * b.i;
  c.i = a.r * b.i + a.i * b.r;
  return c;
}

kiss_fft_cfg speechwarp_priv_fft_alloc(int size, int inverse, void* mem, size_t* lenmem) {
  const double pi = 3.141592653589793238462643383279502884;
  size_t header = round_up(sizeof(plan));
  size_t direct_bytes, kiss_bytes = 0, bytes;
  char* block;
  plan* p;
  int live = (size + 1) / 2, padded = 0, i;

  if (size < 1 || inverse || mem || lenmem) {
    return NULL;
  }
  if (largest_prime_factor(size) > LARGEST_DIRECT_FACTOR) {
    /* Output k needs the chirp at k - j for every live input j: size + live - 1 points in all. */
    padded = 1;
    while (padded < size + live - 1) padded *= 2;
  }

  direct_bytes = kiss_plan_bytes(size);
  bytes = header + direct_bytes;
  if (padded) {
    kiss_bytes = kiss_plan_bytes(padded);
    bytes += 2 * kiss_bytes + round_up((size_t)size * sizeof(kiss_fft_cpx)) +
             3 * round_up((size_t)padded * sizeof(kiss_fft_cpx));
  }
  block = (char*)malloc(bytes);
  if (!block) {
    return NULL;
  }
  p = (plan*)block;
  p->size = size;
  p->live = live;
  p->padded = padded;
  block += header;
  p->direct = kiss_fft_alloc(size, 0, block, &direct_bytes);
  if (!padded) {
    return (kiss_fft_cfg)p;
  }
  block += round_up(direct_bytes);
  p->forward = kiss_fft_alloc(padded, 0, block, &kiss_bytes);
  block += round_up(kiss_bytes);
  p->inverse = kiss_fft_alloc(padded, 1, block, &kiss_bytes);
  block += round_up(kiss_bytes);
  p->chirp = (kiss_fft_cpx*)block;
  block += round_up((size_t)size * sizeof(kiss_fft_cpx));
  p->filter = (kiss_fft_cpx*)block;
  block += round_up((size_t)padded * sizeof(kiss_fft_cpx));
  p->work = (kiss_fft_cpx*)block;
  block += round_up((size_t)padded * sizeof(kiss_fft_cpx));
  p->product = (kiss_fft_cpx*)block;

  for (i = 0; i < size; i++) {
    /* k^2 is reduced first so the angle keeps its precision. */
    double angle = pi * (double)(((long long)i * i) % (2 * size)) / size;
    p->chirp[i].r = (float)cos(angle);
    p->chirp[i].i = (float)-sin(angle);
  }
  for (i = 0; i < padded; i++) {
    p->work[i].r = 0;
    p->work[i].i = 0;
  }
  for (i = 0; i < size; i++) {
    p->work[i].r = p->chirp[i].r / padded;
    p->work[i].i = -p->chirp[i].i / padded;
    if (i > 0 && i < live) {
      p->work[padded - i] = p->work[i];
    }
  }
  kiss_fft(p->forward, p->work, p->filter);
  return (kiss_fft_cfg)p;
}

void speechwarp_priv_fft(kiss_fft_cfg cfg, const kiss_fft_cpx* in, kiss_fft_cpx* out) {
  const plan* p = (const plan*)cfg;
  int i;

  for (i = p->live; i < p->size; i++) {
    if (in[i].r != 0 || in[i].i != 0) break;
  }
  if (!p->padded || i < p->size) {
    kiss_fft(p->direct, in, out);
    return;
  }
  for (i = 0; i < p->live; i++) {
    p->work[i] = multiply(in[i], p->chirp[i]);
  }
  for (; i < p->padded; i++) {
    p->work[i].r = 0;
    p->work[i].i = 0;
  }
  kiss_fft(p->forward, p->work, p->product);
  for (i = 0; i < p->padded; i++) {
    p->product[i] = multiply(p->product[i], p->filter[i]);
  }
  kiss_fft(p->inverse, p->product, p->work);
  for (i = 0; i < p->size; i++) {
    out[i] = multiply(p->work[i], p->chirp[i]);
  }
}
