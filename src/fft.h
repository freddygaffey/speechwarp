/* The FFT Speedy is given in place of KISS FFT's own entry points. See fft.c. */
#ifndef SPEECHWARP_FFT_H_
#define SPEECHWARP_FFT_H_

#include "rename.h"

#include "../third_party/kissfft/kiss_fft.h"

/* Stand-ins for kiss_fft_alloc and kiss_fft. Only what Speedy uses is supported: a forward transform, with
 * the plan in one block that free() releases. `inverse` must be 0 and `mem` and `lenmem` NULL. */
kiss_fft_cfg speechwarp_priv_fft_alloc(int size, int inverse, void* mem, size_t* lenmem);
void speechwarp_priv_fft(kiss_fft_cfg plan, const kiss_fft_cpx* in, kiss_fft_cpx* out);

#endif  /* SPEECHWARP_FFT_H_ */
