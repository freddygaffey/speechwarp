/* Hooks for the tests. Not part of the public API and not exported from the shared library. */
#ifndef SPEECHWARP_INTERNAL_H_
#define SPEECHWARP_INTERNAL_H_

#include "speechwarp.h"

/* Turn the average-speed correction off (0) or on (1, the default). Off reproduces upstream Speedy with
 * sonicSetDurationFeedbackStrength(stream, 0). */
void speechwarp_priv_set_speed_correction(speechwarp_stream* stream, int enabled);

#endif  /* SPEECHWARP_INTERNAL_H_ */
