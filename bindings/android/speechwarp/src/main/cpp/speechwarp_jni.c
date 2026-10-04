/* The JNI layer: the native methods of io.github.freddygaffey.speechwarp.SpeechwarpStream.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * The Kotlin class checks its arguments and that the stream is open, so these functions trust them. A stream
 * travels as a jlong holding the pointer.
 */
#include <jni.h>

#include "speechwarp.h"

#define NATIVE(result, name) \
  JNIEXPORT result JNICALL Java_io_github_freddygaffey_speechwarp_SpeechwarpStream_##name

static speechwarp_stream* stream_of(jlong handle) { return (speechwarp_stream*)(intptr_t)handle; }

NATIVE(jstring, nativeVersion)(JNIEnv* env, jclass type) {
  (void)type;
  return (*env)->NewStringUTF(env, speechwarp_version());
}

NATIVE(jlong, nativeCreate)(JNIEnv* env, jclass type, jint sample_rate, jint channels) {
  (void)env;
  (void)type;
  return (jlong)(intptr_t)speechwarp_create(sample_rate, channels);
}

NATIVE(void, nativeDestroy)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_destroy(stream_of(handle));
}

NATIVE(void, nativeSetSpeed)(JNIEnv* env, jclass type, jlong handle, jfloat speed) {
  (void)env;
  (void)type;
  speechwarp_set_speed(stream_of(handle), speed);
}

NATIVE(jfloat, nativeGetSpeed)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_get_speed(stream_of(handle));
}

NATIVE(void, nativeSetNonlinear)(JNIEnv* env, jclass type, jlong handle, jfloat amount) {
  (void)env;
  (void)type;
  speechwarp_set_nonlinear(stream_of(handle), amount);
}

NATIVE(jfloat, nativeGetNonlinear)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_get_nonlinear(stream_of(handle));
}

/* The options for very high speeds. */
#define FLOAT_OPTION(Name, name)                                                         \
  NATIVE(void, nativeSet##Name)(JNIEnv * env, jclass type, jlong handle, jfloat value) { \
    (void)env;                                                                           \
    (void)type;                                                                          \
    speechwarp_set_##name(stream_of(handle), value);                                     \
  }                                                                                      \
  NATIVE(jfloat, nativeGet##Name)(JNIEnv * env, jclass type, jlong handle) {             \
    (void)env;                                                                           \
    (void)type;                                                                          \
    return speechwarp_get_##name(stream_of(handle));                                     \
  }

FLOAT_OPTION(PauseCap, pause_cap)
FLOAT_OPTION(SpeedFloor, speed_floor)
FLOAT_OPTION(RhythmGap, rhythm_gap)
FLOAT_OPTION(RhythmRate, rhythm_rate)

NATIVE(void, nativeSetKeepSpeed)(JNIEnv* env, jclass type, jlong handle, jboolean enabled) {
  (void)env;
  (void)type;
  speechwarp_set_keep_speed(stream_of(handle), enabled ? 1 : 0);
}

NATIVE(jboolean, nativeGetKeepSpeed)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_get_keep_speed(stream_of(handle)) ? JNI_TRUE : JNI_FALSE;
}

NATIVE(jdouble, nativeSyllableRate)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_syllable_rate(stream_of(handle));
}

NATIVE(jint, nativeAvailable)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_available(stream_of(handle));
}

NATIVE(jlong, nativePosition)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_position(stream_of(handle));
}

NATIVE(jboolean, nativeFlush)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_flush(stream_of(handle)) != 0;
}

NATIVE(void, nativeReset)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_reset(stream_of(handle));
}

/* The arrays are pinned, not copied, where the JVM allows it. `offset` is in samples and `frames` in frames. */

NATIVE(jboolean, nativeWriteFloat)(JNIEnv* env, jclass type, jlong handle, jfloatArray array, jint offset,
                                   jint frames) {
  jfloat* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int ok;
  (void)type;
  if (!samples) return JNI_FALSE;
  ok = speechwarp_write(stream_of(handle), samples + offset, frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, JNI_ABORT);
  return ok != 0;
}

NATIVE(jboolean, nativeWriteShort)(JNIEnv* env, jclass type, jlong handle, jshortArray array, jint offset,
                                   jint frames) {
  jshort* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int ok;
  (void)type;
  if (!samples) return JNI_FALSE;
  ok = speechwarp_write_i16(stream_of(handle), samples + offset, frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, JNI_ABORT);
  return ok != 0;
}

NATIVE(jint, nativeReadFloat)(JNIEnv* env, jclass type, jlong handle, jfloatArray array, jint offset,
                              jint max_frames) {
  jfloat* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int frames;
  (void)type;
  if (!samples) return 0;
  frames = speechwarp_read(stream_of(handle), samples + offset, max_frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, 0);
  return frames;
}

NATIVE(jint, nativeReadShort)(JNIEnv* env, jclass type, jlong handle, jshortArray array, jint offset,
                              jint max_frames) {
  jshort* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int frames;
  (void)type;
  if (!samples) return 0;
  frames = speechwarp_read_i16(stream_of(handle), samples + offset, max_frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, 0);
  return frames;
}
