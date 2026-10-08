/* The JNI layer: the native methods of io.github.freddygaffey.speechwarp.SpeechwarpStream, SyllableCounter,
 * ListenerTrainer and BlindTrials. The Android (Kotlin) and desktop (Java) libraries share this file, so the
 * native method names and signatures of both must stay the same.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 *
 * The Kotlin and Java classes check their arguments and that the object is open, so these functions trust them.
 * An object travels as a jlong holding the pointer.
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

NATIVE(void, nativeSetHeardPause)(JNIEnv* env, jclass type, jlong handle, jfloat seconds, jfloat from_speed) {
  (void)env;
  (void)type;
  speechwarp_set_heard_pause(stream_of(handle), seconds, from_speed);
}

NATIVE(void, nativeSetFloorBlend)(JNIEnv* env, jclass type, jlong handle, jfloat fraction, jfloat from_speed,
                                  jfloat full_speed) {
  (void)env;
  (void)type;
  speechwarp_set_floor_blend(stream_of(handle), fraction, from_speed, full_speed);
}

#define FLOAT_GETTER(Name, name)                                             \
  NATIVE(jfloat, nativeGet##Name)(JNIEnv * env, jclass type, jlong handle) { \
    (void)env;                                                               \
    (void)type;                                                              \
    return speechwarp_get_##name(stream_of(handle));                         \
  }

FLOAT_GETTER(HeardPause, heard_pause)
FLOAT_GETTER(HeardPauseFrom, heard_pause_from)
FLOAT_GETTER(FloorBlend, floor_blend)
FLOAT_GETTER(FloorBlendFrom, floor_blend_from)
FLOAT_GETTER(FloorBlendFull, floor_blend_full)

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

/* ---- SyllableCounter ---------------------------------------------------------------------------------- */

#define COUNTER(result, name) \
  JNIEXPORT result JNICALL Java_io_github_freddygaffey_speechwarp_SyllableCounter_##name

static speechwarp_syllables* counter_of(jlong handle) { return (speechwarp_syllables*)(intptr_t)handle; }

COUNTER(jlong, nativeCreate)(JNIEnv* env, jclass type, jint sample_rate, jint channels) {
  (void)env;
  (void)type;
  return (jlong)(intptr_t)speechwarp_syllables_create(sample_rate, channels);
}

COUNTER(void, nativeDestroy)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_syllables_destroy(counter_of(handle));
}

COUNTER(jboolean, nativeWriteFloat)(JNIEnv* env, jclass type, jlong handle, jfloatArray array, jint offset,
                                    jint frames) {
  jfloat* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int ok;
  (void)type;
  if (!samples) return JNI_FALSE;
  ok = speechwarp_syllables_write(counter_of(handle), samples + offset, frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, JNI_ABORT);
  return ok != 0;
}

COUNTER(jboolean, nativeWriteShort)(JNIEnv* env, jclass type, jlong handle, jshortArray array, jint offset,
                                    jint frames) {
  jshort* samples = (*env)->GetPrimitiveArrayCritical(env, array, NULL);
  int ok;
  (void)type;
  if (!samples) return JNI_FALSE;
  ok = speechwarp_syllables_write_i16(counter_of(handle), samples + offset, frames);
  (*env)->ReleasePrimitiveArrayCritical(env, array, samples, JNI_ABORT);
  return ok != 0;
}

COUNTER(jdouble, nativeRate)(JNIEnv* env, jclass type, jlong handle, jdouble window, jdouble minimum) {
  (void)env;
  (void)type;
  return speechwarp_syllables_rate(counter_of(handle), window, minimum);
}

COUNTER(void, nativeReset)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_syllables_reset(counter_of(handle));
}

/* ---- ListenerTrainer ---------------------------------------------------------------------------------- */

#define TRAINER(result, name) \
  JNIEXPORT result JNICALL Java_io_github_freddygaffey_speechwarp_ListenerTrainer_##name

static speechwarp_trainer* trainer_of(jlong handle) { return (speechwarp_trainer*)(intptr_t)handle; }

/* The seed is the bit pattern of the unsigned 64-bit seed. */
TRAINER(jlong, nativeCreate)(JNIEnv* env, jclass type, jlong seed) {
  (void)env;
  (void)type;
  return (jlong)(intptr_t)speechwarp_trainer_create((uint64_t)seed);
}

TRAINER(void, nativeDestroy)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_trainer_destroy(trainer_of(handle));
}

TRAINER(void, nativeSetWeight)(JNIEnv* env, jclass type, jlong handle, jint kind, jdouble weight) {
  (void)env;
  (void)type;
  speechwarp_trainer_set_weight(trainer_of(handle), kind, weight);
}

TRAINER(jdouble, nativeGetWeight)(JNIEnv* env, jclass type, jlong handle, jint kind) {
  (void)env;
  (void)type;
  return speechwarp_trainer_get_weight(trainer_of(handle), kind);
}

TRAINER(void, nativeSetParam)(JNIEnv* env, jclass type, jlong handle, jint param, jdouble value) {
  (void)env;
  (void)type;
  speechwarp_trainer_set_param(trainer_of(handle), param, value);
}

TRAINER(jdouble, nativeGetParam)(JNIEnv* env, jclass type, jlong handle, jint param) {
  (void)env;
  (void)type;
  return speechwarp_trainer_get_param(trainer_of(handle), param);
}

TRAINER(jboolean, nativeAddMeasure)(JNIEnv* env, jclass type, jlong handle, jint kind, jdouble score,
                                    jdouble items, jdouble rate, jdouble time) {
  (void)env;
  (void)type;
  return speechwarp_trainer_add_measure(trainer_of(handle), kind, score, items, rate, time) != 0;
}

TRAINER(void, nativeTestBegin)(JNIEnv* env, jclass type, jlong handle, jdouble prior_rate, jdouble time) {
  (void)env;
  (void)type;
  speechwarp_trainer_test_begin(trainer_of(handle), prior_rate, time);
}

TRAINER(jdouble, nativeTestRate)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_test_rate(trainer_of(handle));
}

TRAINER(jboolean, nativeTestDone)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_test_done(trainer_of(handle)) != 0;
}

TRAINER(jdouble, nativeTestEnd)(JNIEnv* env, jclass type, jlong handle, jdouble time) {
  (void)env;
  (void)type;
  return speechwarp_trainer_test_end(trainer_of(handle), time);
}

TRAINER(jdouble, nativeThreshold)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_threshold(trainer_of(handle));
}

TRAINER(jdouble, nativeThresholdLow)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_threshold_low(trainer_of(handle));
}

TRAINER(jdouble, nativeThresholdHigh)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_threshold_high(trainer_of(handle));
}

TRAINER(void, nativeSessionBegin)(JNIEnv* env, jclass type, jlong handle, jint plan, jdouble time) {
  (void)env;
  (void)type;
  speechwarp_trainer_session_begin(trainer_of(handle), plan, time);
}

TRAINER(jdouble, nativeSessionRate)(JNIEnv* env, jclass type, jlong handle, jdouble time) {
  (void)env;
  (void)type;
  return speechwarp_trainer_session_rate(trainer_of(handle), time);
}

TRAINER(jint, nativeSessionEnd)(JNIEnv* env, jclass type, jlong handle, jdouble hours, jdouble time) {
  (void)env;
  (void)type;
  return speechwarp_trainer_session_end(trainer_of(handle), hours, time);
}

TRAINER(jboolean, nativeAddRetention)(JNIEnv* env, jclass type, jlong handle, jint session, jdouble score,
                                      jdouble items, jdouble delay_seconds, jdouble time) {
  (void)env;
  (void)type;
  return speechwarp_trainer_add_retention(trainer_of(handle), session, score, items, delay_seconds, time) != 0;
}

TRAINER(jint, nativeNextPlan)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_next_plan(trainer_of(handle));
}

#define PLAN_DOUBLE(Name, name)                                                         \
  TRAINER(jdouble, nativePlan##Name)(JNIEnv * env, jclass type, jlong handle, jint plan) { \
    (void)env;                                                                          \
    (void)type;                                                                         \
    return speechwarp_trainer_plan_##name(trainer_of(handle), plan);                    \
  }

PLAN_DOUBLE(Effect, effect)
PLAN_DOUBLE(EffectSd, effect_sd)
PLAN_DOUBLE(Retention, retention)
PLAN_DOUBLE(RetentionSd, retention_sd)
PLAN_DOUBLE(BestProbability, best_probability)

TRAINER(jint, nativePlanSessions)(JNIEnv* env, jclass type, jlong handle, jint plan) {
  (void)env;
  (void)type;
  return speechwarp_trainer_plan_sessions(trainer_of(handle), plan);
}

TRAINER(jdouble, nativeTrend)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_trend(trainer_of(handle));
}

TRAINER(jdouble, nativeTrendSd)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trainer_trend_sd(trainer_of(handle));
}

/* ---- BlindTrials -------------------------------------------------------------------------------------- */

#define TRIALS(result, name) \
  JNIEXPORT result JNICALL Java_io_github_freddygaffey_speechwarp_BlindTrials_##name

static speechwarp_trials* trials_of(jlong handle) { return (speechwarp_trials*)(intptr_t)handle; }

/* The seed is the bit pattern of the unsigned 64-bit seed. */
TRIALS(jlong, nativeCreate)(JNIEnv* env, jclass type, jlong seed) {
  (void)env;
  (void)type;
  return (jlong)(intptr_t)speechwarp_trials_create((uint64_t)seed);
}

TRIALS(void, nativeDestroy)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  speechwarp_trials_destroy(trials_of(handle));
}

TRIALS(jint, nativeAddSetting)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trials_add_setting(trials_of(handle));
}

TRIALS(jint, nativeAddValue)(JNIEnv* env, jclass type, jlong handle, jint setting, jdouble value) {
  (void)env;
  (void)type;
  return speechwarp_trials_add_value(trials_of(handle), setting, value);
}

TRIALS(void, nativeSetAvailable)(JNIEnv* env, jclass type, jlong handle, jint setting, jboolean available) {
  (void)env;
  (void)type;
  speechwarp_trials_set_available(trials_of(handle), setting, available ? 1 : 0);
}

TRIALS(jboolean, nativeAdd)(JNIEnv* env, jclass type, jlong handle, jint setting, jdouble speed,
                            jdouble first_value, jdouble second_value, jdouble first_score,
                            jdouble second_score, jint preferred) {
  (void)env;
  (void)type;
  return speechwarp_trials_add(trials_of(handle), setting, speed, first_value, second_value, first_score,
                               second_score, preferred) != 0;
}

TRIALS(jint, nativeNext)(JNIEnv* env, jclass type, jlong handle, jdouble speed) {
  (void)env;
  (void)type;
  return speechwarp_trials_next(trials_of(handle), speed);
}

TRIALS(jdouble, nativeNextFirst)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trials_next_first(trials_of(handle));
}

TRIALS(jdouble, nativeNextSecond)(JNIEnv* env, jclass type, jlong handle) {
  (void)env;
  (void)type;
  return speechwarp_trials_next_second(trials_of(handle));
}

#define TRIALS_COUNT(Name, name)                                                                       \
  TRIALS(jint, native##Name)(JNIEnv * env, jclass type, jlong handle, jint setting, jdouble speed, \
                             jint value) {                                                             \
    (void)env;                                                                                         \
    (void)type;                                                                                        \
    return speechwarp_trials_##name(trials_of(handle), setting, speed, value);                         \
  }

TRIALS_COUNT(Won, won)
TRIALS_COUNT(Lost, lost)
TRIALS_COUNT(Tied, tied)
TRIALS_COUNT(Heard, heard)

TRIALS(jdouble, nativeMeanScore)(JNIEnv* env, jclass type, jlong handle, jint setting, jdouble speed,
                                 jint value) {
  (void)env;
  (void)type;
  return speechwarp_trials_mean_score(trials_of(handle), setting, speed, value);
}

TRIALS(jint, nativeWinner)(JNIEnv* env, jclass type, jlong handle, jint setting, jdouble speed) {
  (void)env;
  (void)type;
  return speechwarp_trials_winner(trials_of(handle), setting, speed);
}

TRIALS(void, nativeSetConfidence)(JNIEnv* env, jclass type, jlong handle, jdouble confidence) {
  (void)env;
  (void)type;
  speechwarp_trials_set_confidence(trials_of(handle), confidence);
}
