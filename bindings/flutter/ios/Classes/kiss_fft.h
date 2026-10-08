// Lets Speedy find KISS FFT's header without a search path; see speechwarp.h beside this file.
#if __has_include("../../src/speechwarp/third_party/kissfft/kiss_fft.h")
#include "../../src/speechwarp/third_party/kissfft/kiss_fft.h"
#else
#include "../../../../third_party/kissfft/kiss_fft.h"
#endif
