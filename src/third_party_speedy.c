/* Speedy's analysis, using KISS FFT rather than FFTW, through fft.c so that it is fast at every sample rate.
 * Needs third_party/kissfft on the include path. */
#include "fft.h"

#undef kiss_fft_alloc
#undef kiss_fft
#define kiss_fft_alloc speechwarp_priv_fft_alloc
#define kiss_fft speechwarp_priv_fft

#define KISS_FFT 1
#include "../third_party/speedy/speedy.c"
