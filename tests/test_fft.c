/* Checks src/fft.c against a transform computed the slow, obvious way. */
#include "fft.h"
#include "test_util.h"

/* `live` is how many points of input are not zero: Speedy leaves the second half zero. */
static void check_size(int size, int live) {
  const double two_pi = 6.283185307179586;
  kiss_fft_cfg plan = speechwarp_priv_fft_alloc(size, 0, NULL, NULL);
  kiss_fft_cpx* in = (kiss_fft_cpx*)malloc((size_t)size * sizeof(kiss_fft_cpx));
  kiss_fft_cpx* out = (kiss_fft_cpx*)malloc((size_t)size * sizeof(kiss_fft_cpx));
  double worst = 0, largest = 0;
  int i, k;

  CHECK(plan != NULL);
  random_state = (uint32_t)size;
  for (i = 0; i < size; i++) {
    in[i].r = i < live ? (float)(random_unit() - 0.5) : 0;
    in[i].i = i < live ? (float)(random_unit() - 0.5) : 0;
  }
  speechwarp_priv_fft(plan, in, out);
  for (k = 0; k < size; k++) {
    double re = 0, im = 0, error;
    for (i = 0; i < size; i++) {
      double angle = -two_pi * (double)(((long long)i * k) % size) / size;
      re += in[i].r * cos(angle) - in[i].i * sin(angle);
      im += in[i].r * sin(angle) + in[i].i * cos(angle);
    }
    error = hypot(out[k].r - re, out[k].i - im);
    if (error > worst) worst = error;
    if (hypot(re, im) > largest) largest = hypot(re, im);
  }
  if (worst > 1e-5 * largest) {
    printf("size %d, %d live: error %g against a largest value of %g\n", size, live, worst, largest);
  }
  CHECK(worst <= 1e-5 * largest);
  free(plan);
  free(in);
  free(out);
}

int main(void) {
  /* 1322 is the size at 44.1 kHz and 660 at 22.05 kHz; the rest cover each path and the edges. */
  static const int sizes[] = {1, 2, 3, 37, 64, 74, 127, 660, 1322, 1440, 2646, 4099};
  size_t i;

  for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    check_size(sizes[i], sizes[i]);
    check_size(sizes[i], (sizes[i] + 1) / 2);
    check_size(sizes[i], (sizes[i] + 1) / 2 + 1);
    check_size(sizes[i], 1);
  }
  CHECK(speechwarp_priv_fft_alloc(0, 0, NULL, NULL) == NULL);
  CHECK(speechwarp_priv_fft_alloc(64, 1, NULL, NULL) == NULL);

  if (failures) {
    printf("%d checks failed\n", failures);
    return 1;
  }
  printf("all passed\n");
  return 0;
}
