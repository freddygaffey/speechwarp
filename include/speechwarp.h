/* speechwarp - nonlinear speed-up for speech.
 *
 * A small, stable C API over Google's Speedy algorithm and the Sonic library.
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * Usage:
 *     speechwarp_stream* s = speechwarp_create(44100, 2);
 *     speechwarp_set_speed(s, 3.0f);
 *     for each chunk of input:
 *         speechwarp_write(s, in, frames);
 *         while ((n = speechwarp_read(s, out, capacity)) > 0) play(out, n);
 *     speechwarp_flush(s);  then read until empty
 *     speechwarp_destroy(s);
 *
 * Samples are interleaved 32-bit floats in [-1, 1], or 16-bit integers. A "frame" is one sample per channel.
 * A stream is not thread safe: use it from one thread, or lock around it.
 */
#ifndef SPEECHWARP_H_
#define SPEECHWARP_H_

#include <stdint.h>

#if defined(_WIN32)
#  if defined(SPEECHWARP_BUILD)
#    define SPEECHWARP_API __declspec(dllexport)
#  elif defined(SPEECHWARP_SHARED)
#    define SPEECHWARP_API __declspec(dllimport)
#  else
#    define SPEECHWARP_API
#  endif
#else
#  define SPEECHWARP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SPEECHWARP_VERSION_MAJOR 0
#define SPEECHWARP_VERSION_MINOR 1
#define SPEECHWARP_VERSION_PATCH 0
#define SPEECHWARP_VERSION "0.1.0"

#define SPEECHWARP_MIN_SPEED 0.05f
#define SPEECHWARP_MAX_SPEED 20.0f

typedef struct speechwarp_stream speechwarp_stream;

/* The version of the library in use, e.g. "0.1.0". */
SPEECHWARP_API const char* speechwarp_version(void);

/* Create a stream. Returns NULL if the arguments are invalid or memory runs out. The sample rate must be
 * 4000 to 384000 Hz and there may be 1 to 32 channels. Starts at speed 1 with nonlinear speed-up on. */
SPEECHWARP_API speechwarp_stream* speechwarp_create(int sample_rate, int channels);
SPEECHWARP_API void speechwarp_destroy(speechwarp_stream* stream);

/* Overall speed: 2 plays twice as fast, so ten minutes of speech comes out in about five. Clamped to
 * SPEECHWARP_MIN_SPEED..SPEECHWARP_MAX_SPEED; zero, negative and NaN are ignored. Takes effect on audio not
 * yet processed, which includes the last 0.15 s or so written.
 *
 * With nonlinear speed-up the speed varies from moment to moment, and the stream steers its average to this
 * value within a few seconds of input. It only steers by speeding up, so a recording with long silences
 * finishes somewhat sooner than the speed suggests. */
SPEECHWARP_API void speechwarp_set_speed(speechwarp_stream* stream, float speed);
SPEECHWARP_API float speechwarp_get_speed(const speechwarp_stream* stream);

/* How unevenly time is compressed, 0 to 1. 1 (the default) is Speedy: consonants and transitions are slowed
 * and vowels and pauses hurried, as a fast talker does. 0 compresses everything evenly (plain Sonic). Values
 * in between are allowed but untested upstream. May be changed at any time. */
SPEECHWARP_API void speechwarp_set_nonlinear(speechwarp_stream* stream, float amount);
SPEECHWARP_API float speechwarp_get_nonlinear(const speechwarp_stream* stream);

/* Add input. Returns 1 on success, 0 if memory ran out. The output does not depend on how the input is
 * divided between calls. */
SPEECHWARP_API int speechwarp_write(speechwarp_stream* stream, const float* samples, int frames);
SPEECHWARP_API int speechwarp_write_i16(speechwarp_stream* stream, const int16_t* samples, int frames);

/* Take processed output. Returns the number of frames written to `samples`, which may be 0: output lags
 * input by a look-ahead of about 0.15 s of input with nonlinear speed-up, and 0.04 s without. */
SPEECHWARP_API int speechwarp_read(speechwarp_stream* stream, float* samples, int max_frames);
SPEECHWARP_API int speechwarp_read_i16(speechwarp_stream* stream, int16_t* samples, int max_frames);

/* Frames of output ready to read. */
SPEECHWARP_API int speechwarp_available(const speechwarp_stream* stream);

/* Process everything written so far, at the end of the input. Returns 1 on success, 0 if memory ran out.
 * Writing more afterwards starts a new stretch of audio; position carries on counting. */
SPEECHWARP_API int speechwarp_flush(speechwarp_stream* stream);

/* Discard all buffered input and output, keeping speed and nonlinear settings. Use after seeking. In the
 * unlikely event that memory runs out here, the stream stops accepting input: write and flush return 0. */
SPEECHWARP_API void speechwarp_reset(speechwarp_stream* stream);

/* The input frame, counted from creation or the last reset, that the next output frame to be read was made
 * from. Because speed varies from moment to moment, this is how a player maps what is being heard back to
 * a position in the source. It never goes backwards, it is approximate (it can run up to about 0.05 s of
 * input ahead), and once everything after a flush has been read it equals the number of frames written. */
SPEECHWARP_API int64_t speechwarp_position(const speechwarp_stream* stream);

#ifdef __cplusplus
}
#endif

#endif  /* SPEECHWARP_H_ */
