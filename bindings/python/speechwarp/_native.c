/* The Python extension: a thin layer over include/speechwarp.h. bindings/python/speechwarp/__init__.py turns
 * it into the API people use; this file only moves buffers across.
 *
 * Licensed under the Apache License, Version 2.0. See LICENSE and NOTICE.
 */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "speechwarp.h"

#define CAPSULE_NAME "speechwarp.stream"

static void destroy_capsule(PyObject* capsule) {
  speechwarp_destroy((speechwarp_stream*)PyCapsule_GetPointer(capsule, CAPSULE_NAME));
}

static speechwarp_stream* stream_of(PyObject* capsule) {
  return (speechwarp_stream*)PyCapsule_GetPointer(capsule, CAPSULE_NAME);
}

static PyObject* native_create(PyObject* self, PyObject* args) {
  int sample_rate, channels;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "ii", &sample_rate, &channels)) return NULL;
  stream = speechwarp_create(sample_rate, channels);
  if (!stream) return PyErr_NoMemory();
  return PyCapsule_New(stream, CAPSULE_NAME, destroy_capsule);
}

static PyObject* native_set_speed(PyObject* self, PyObject* args) {
  PyObject* capsule;
  float value;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Of", &capsule, &value) || !(stream = stream_of(capsule))) return NULL;
  speechwarp_set_speed(stream, value);
  Py_RETURN_NONE;
}

static PyObject* native_set_nonlinear(PyObject* self, PyObject* args) {
  PyObject* capsule;
  float value;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Of", &capsule, &value) || !(stream = stream_of(capsule))) return NULL;
  speechwarp_set_nonlinear(stream, value);
  Py_RETURN_NONE;
}

static PyObject* native_get_speed(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  return stream ? PyFloat_FromDouble(speechwarp_get_speed(stream)) : NULL;
}

static PyObject* native_get_nonlinear(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  return stream ? PyFloat_FromDouble(speechwarp_get_nonlinear(stream)) : NULL;
}

/* The options for very high speeds, which all take and give one number. */
#define OPTION(name, type, from_python, to_python)                                                       \
  static PyObject* native_set_##name(PyObject* self, PyObject* args) {                                   \
    PyObject* capsule;                                                                                    \
    type value;                                                                                           \
    speechwarp_stream* stream;                                                                            \
    (void)self;                                                                                           \
    if (!PyArg_ParseTuple(args, "O" from_python, &capsule, &value) || !(stream = stream_of(capsule))) {   \
      return NULL;                                                                                        \
    }                                                                                                     \
    speechwarp_set_##name(stream, value);                                                                 \
    Py_RETURN_NONE;                                                                                       \
  }                                                                                                       \
  static PyObject* native_get_##name(PyObject* self, PyObject* capsule) {                                \
    speechwarp_stream* stream = stream_of(capsule);                                                       \
    (void)self;                                                                                           \
    return stream ? to_python(speechwarp_get_##name(stream)) : NULL;                                      \
  }

OPTION(pause_cap, float, "f", PyFloat_FromDouble)
OPTION(keep_speed, int, "p", PyBool_FromLong)
OPTION(speed_floor, float, "f", PyFloat_FromDouble)
OPTION(rhythm_gap, float, "f", PyFloat_FromDouble)
OPTION(rhythm_rate, float, "f", PyFloat_FromDouble)

static PyObject* native_set_heard_pause(PyObject* self, PyObject* args) {
  PyObject* capsule;
  float seconds, from_speed;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Off", &capsule, &seconds, &from_speed) || !(stream = stream_of(capsule))) {
    return NULL;
  }
  speechwarp_set_heard_pause(stream, seconds, from_speed);
  Py_RETURN_NONE;
}

static PyObject* native_set_floor_blend(PyObject* self, PyObject* args) {
  PyObject* capsule;
  float fraction, from_speed, full_speed;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Offf", &capsule, &fraction, &from_speed, &full_speed) ||
      !(stream = stream_of(capsule))) {
    return NULL;
  }
  speechwarp_set_floor_blend(stream, fraction, from_speed, full_speed);
  Py_RETURN_NONE;
}

/* Getters that take only the stream and give a float. */
#define STREAM_FLOAT_GETTER(name)                                                     \
  static PyObject* native_get_##name(PyObject* self, PyObject* capsule) {             \
    speechwarp_stream* stream = stream_of(capsule);                                   \
    (void)self;                                                                       \
    return stream ? PyFloat_FromDouble(speechwarp_get_##name(stream)) : NULL;         \
  }

STREAM_FLOAT_GETTER(heard_pause)
STREAM_FLOAT_GETTER(heard_pause_from)
STREAM_FLOAT_GETTER(floor_blend)
STREAM_FLOAT_GETTER(floor_blend_from)
STREAM_FLOAT_GETTER(floor_blend_full)

/* ---- Syllable counter ---- */

#define COUNTER_CAPSULE_NAME "speechwarp.syllables"

static void destroy_counter_capsule(PyObject* capsule) {
  speechwarp_syllables_destroy((speechwarp_syllables*)PyCapsule_GetPointer(capsule, COUNTER_CAPSULE_NAME));
}

static speechwarp_syllables* counter_of(PyObject* capsule) {
  return (speechwarp_syllables*)PyCapsule_GetPointer(capsule, COUNTER_CAPSULE_NAME);
}

static PyObject* native_syllables_create(PyObject* self, PyObject* args) {
  int sample_rate, channels;
  speechwarp_syllables* counter;
  (void)self;
  if (!PyArg_ParseTuple(args, "ii", &sample_rate, &channels)) return NULL;
  counter = speechwarp_syllables_create(sample_rate, channels);
  if (!counter) return PyErr_NoMemory();
  return PyCapsule_New(counter, COUNTER_CAPSULE_NAME, destroy_counter_capsule);
}

/* syllables_write(counter, buffer, frames, is_float) */
static PyObject* native_syllables_write(PyObject* self, PyObject* args) {
  PyObject* capsule;
  Py_buffer buffer;
  int frames, is_float, ok;
  speechwarp_syllables* counter;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oy*ip", &capsule, &buffer, &frames, &is_float)) return NULL;
  counter = counter_of(capsule);
  if (!counter) {
    PyBuffer_Release(&buffer);
    return NULL;
  }
  Py_BEGIN_ALLOW_THREADS
  ok = is_float ? speechwarp_syllables_write(counter, (const float*)buffer.buf, frames)
                : speechwarp_syllables_write_i16(counter, (const int16_t*)buffer.buf, frames);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&buffer);
  if (!ok) {
    PyErr_SetString(PyExc_ValueError, "invalid arguments to the syllable counter");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* native_syllables_rate(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double window, minimum;
  speechwarp_syllables* counter;
  (void)self;
  if (!PyArg_ParseTuple(args, "Odd", &capsule, &window, &minimum) || !(counter = counter_of(capsule))) {
    return NULL;
  }
  return PyFloat_FromDouble(speechwarp_syllables_rate(counter, window, minimum));
}

static PyObject* native_syllables_reset(PyObject* self, PyObject* capsule) {
  speechwarp_syllables* counter = counter_of(capsule);
  (void)self;
  if (!counter) return NULL;
  speechwarp_syllables_reset(counter);
  Py_RETURN_NONE;
}

/* ---- Listener trainer ---- */

#define TRAINER_CAPSULE_NAME "speechwarp.trainer"

static void destroy_trainer_capsule(PyObject* capsule) {
  speechwarp_trainer_destroy((speechwarp_trainer*)PyCapsule_GetPointer(capsule, TRAINER_CAPSULE_NAME));
}

static speechwarp_trainer* trainer_of(PyObject* capsule) {
  return (speechwarp_trainer*)PyCapsule_GetPointer(capsule, TRAINER_CAPSULE_NAME);
}

static PyObject* native_trainer_create(PyObject* self, PyObject* args) {
  unsigned long long seed;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "K", &seed)) return NULL;
  trainer = speechwarp_trainer_create((uint64_t)seed);
  if (!trainer) return PyErr_NoMemory();
  return PyCapsule_New(trainer, TRAINER_CAPSULE_NAME, destroy_trainer_capsule);
}

static PyObject* native_trainer_set_weight(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int kind;
  double weight;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oid", &capsule, &kind, &weight) || !(trainer = trainer_of(capsule))) return NULL;
  speechwarp_trainer_set_weight(trainer, kind, weight);
  Py_RETURN_NONE;
}

static PyObject* native_trainer_get_weight(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int kind;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oi", &capsule, &kind) || !(trainer = trainer_of(capsule))) return NULL;
  return PyFloat_FromDouble(speechwarp_trainer_get_weight(trainer, kind));
}

static PyObject* native_trainer_set_param(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int param;
  double value;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oid", &capsule, &param, &value) || !(trainer = trainer_of(capsule))) return NULL;
  speechwarp_trainer_set_param(trainer, param, value);
  Py_RETURN_NONE;
}

static PyObject* native_trainer_get_param(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int param;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oi", &capsule, &param) || !(trainer = trainer_of(capsule))) return NULL;
  return PyFloat_FromDouble(speechwarp_trainer_get_param(trainer, param));
}

static PyObject* native_trainer_add_measure(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int kind;
  double score, items, rate, time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oidddd", &capsule, &kind, &score, &items, &rate, &time) ||
      !(trainer = trainer_of(capsule))) {
    return NULL;
  }
  return PyBool_FromLong(speechwarp_trainer_add_measure(trainer, kind, score, items, rate, time));
}

static PyObject* native_trainer_test_begin(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double prior_rate, time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Odd", &capsule, &prior_rate, &time) || !(trainer = trainer_of(capsule))) {
    return NULL;
  }
  speechwarp_trainer_test_begin(trainer, prior_rate, time);
  Py_RETURN_NONE;
}

static PyObject* native_trainer_test_rate(PyObject* self, PyObject* capsule) {
  speechwarp_trainer* trainer = trainer_of(capsule);
  (void)self;
  return trainer ? PyFloat_FromDouble(speechwarp_trainer_test_rate(trainer)) : NULL;
}

static PyObject* native_trainer_test_done(PyObject* self, PyObject* capsule) {
  speechwarp_trainer* trainer = trainer_of(capsule);
  (void)self;
  return trainer ? PyBool_FromLong(speechwarp_trainer_test_done(trainer)) : NULL;
}

static PyObject* native_trainer_test_end(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Od", &capsule, &time) || !(trainer = trainer_of(capsule))) return NULL;
  return PyFloat_FromDouble(speechwarp_trainer_test_end(trainer, time));
}

#define TRAINER_GETTER(name)                                                              \
  static PyObject* native_trainer_##name(PyObject* self, PyObject* capsule) {             \
    speechwarp_trainer* trainer = trainer_of(capsule);                                    \
    (void)self;                                                                           \
    return trainer ? PyFloat_FromDouble(speechwarp_trainer_##name(trainer)) : NULL;       \
  }

TRAINER_GETTER(threshold)
TRAINER_GETTER(threshold_low)
TRAINER_GETTER(threshold_high)
TRAINER_GETTER(trend)
TRAINER_GETTER(trend_sd)

static PyObject* native_trainer_session_begin(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int plan;
  double time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oid", &capsule, &plan, &time) || !(trainer = trainer_of(capsule))) return NULL;
  speechwarp_trainer_session_begin(trainer, plan, time);
  Py_RETURN_NONE;
}

static PyObject* native_trainer_session_rate(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Od", &capsule, &time) || !(trainer = trainer_of(capsule))) return NULL;
  return PyFloat_FromDouble(speechwarp_trainer_session_rate(trainer, time));
}

static PyObject* native_trainer_session_end(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double hours, time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Odd", &capsule, &hours, &time) || !(trainer = trainer_of(capsule))) return NULL;
  return PyLong_FromLong(speechwarp_trainer_session_end(trainer, hours, time));
}

static PyObject* native_trainer_add_retention(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int session;
  double score, items, delay, time;
  speechwarp_trainer* trainer;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oidddd", &capsule, &session, &score, &items, &delay, &time) ||
      !(trainer = trainer_of(capsule))) {
    return NULL;
  }
  return PyBool_FromLong(speechwarp_trainer_add_retention(trainer, session, score, items, delay, time));
}

static PyObject* native_trainer_next_plan(PyObject* self, PyObject* capsule) {
  speechwarp_trainer* trainer = trainer_of(capsule);
  (void)self;
  return trainer ? PyLong_FromLong(speechwarp_trainer_next_plan(trainer)) : NULL;
}

#define TRAINER_PLAN_GETTER(name, make)                                                    \
  static PyObject* native_trainer_##name(PyObject* self, PyObject* args) {                 \
    PyObject* capsule;                                                                     \
    int plan;                                                                              \
    speechwarp_trainer* trainer;                                                           \
    (void)self;                                                                            \
    if (!PyArg_ParseTuple(args, "Oi", &capsule, &plan) || !(trainer = trainer_of(capsule))) \
      return NULL;                                                                         \
    return make(speechwarp_trainer_##name(trainer, plan));                                 \
  }

TRAINER_PLAN_GETTER(plan_effect, PyFloat_FromDouble)
TRAINER_PLAN_GETTER(plan_effect_sd, PyFloat_FromDouble)
TRAINER_PLAN_GETTER(plan_retention, PyFloat_FromDouble)
TRAINER_PLAN_GETTER(plan_retention_sd, PyFloat_FromDouble)
TRAINER_PLAN_GETTER(plan_sessions, PyLong_FromLong)
TRAINER_PLAN_GETTER(plan_best_probability, PyFloat_FromDouble)

/* ---- Blind trials ---- */

#define TRIALS_CAPSULE_NAME "speechwarp.trials"

static void destroy_trials_capsule(PyObject* capsule) {
  speechwarp_trials_destroy((speechwarp_trials*)PyCapsule_GetPointer(capsule, TRIALS_CAPSULE_NAME));
}

static speechwarp_trials* trials_of(PyObject* capsule) {
  return (speechwarp_trials*)PyCapsule_GetPointer(capsule, TRIALS_CAPSULE_NAME);
}

static PyObject* native_trials_create(PyObject* self, PyObject* args) {
  unsigned long long seed;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "K", &seed)) return NULL;
  trials = speechwarp_trials_create((uint64_t)seed);
  if (!trials) return PyErr_NoMemory();
  return PyCapsule_New(trials, TRIALS_CAPSULE_NAME, destroy_trials_capsule);
}

static PyObject* native_trials_add_setting(PyObject* self, PyObject* capsule) {
  speechwarp_trials* trials = trials_of(capsule);
  (void)self;
  return trials ? PyLong_FromLong(speechwarp_trials_add_setting(trials)) : NULL;
}

static PyObject* native_trials_add_value(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int setting;
  double value;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oid", &capsule, &setting, &value) || !(trials = trials_of(capsule))) return NULL;
  return PyLong_FromLong(speechwarp_trials_add_value(trials, setting, value));
}

static PyObject* native_trials_set_available(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int setting, available;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oip", &capsule, &setting, &available) || !(trials = trials_of(capsule))) {
    return NULL;
  }
  speechwarp_trials_set_available(trials, setting, available);
  Py_RETURN_NONE;
}

static PyObject* native_trials_add(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int setting, preferred;
  double speed, first_value, second_value, first_score, second_score;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oidddddi", &capsule, &setting, &speed, &first_value, &second_value, &first_score,
                        &second_score, &preferred) ||
      !(trials = trials_of(capsule))) {
    return NULL;
  }
  return PyBool_FromLong(speechwarp_trials_add(trials, setting, speed, first_value, second_value, first_score,
                                               second_score, preferred));
}

/* trials_next(trials, speed) -> (setting, first, second); setting is -1 when there is nothing to compare. */
static PyObject* native_trials_next(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double speed;
  int setting;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Od", &capsule, &speed) || !(trials = trials_of(capsule))) return NULL;
  setting = speechwarp_trials_next(trials, speed);
  return Py_BuildValue("idd", setting, speechwarp_trials_next_first(trials), speechwarp_trials_next_second(trials));
}

#define TRIALS_RESULT(name, make)                                                                        \
  static PyObject* native_trials_##name(PyObject* self, PyObject* args) {                                \
    PyObject* capsule;                                                                                   \
    int setting, value;                                                                                  \
    double speed;                                                                                        \
    speechwarp_trials* trials;                                                                           \
    (void)self;                                                                                          \
    if (!PyArg_ParseTuple(args, "Oidi", &capsule, &setting, &speed, &value) || !(trials = trials_of(capsule))) \
      return NULL;                                                                                       \
    return make(speechwarp_trials_##name(trials, setting, speed, value));                                \
  }

TRIALS_RESULT(won, PyLong_FromLong)
TRIALS_RESULT(lost, PyLong_FromLong)
TRIALS_RESULT(tied, PyLong_FromLong)
TRIALS_RESULT(heard, PyLong_FromLong)
TRIALS_RESULT(mean_score, PyFloat_FromDouble)

static PyObject* native_trials_winner(PyObject* self, PyObject* args) {
  PyObject* capsule;
  int setting;
  double speed;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oid", &capsule, &setting, &speed) || !(trials = trials_of(capsule))) return NULL;
  return PyLong_FromLong(speechwarp_trials_winner(trials, setting, speed));
}

static PyObject* native_trials_set_confidence(PyObject* self, PyObject* args) {
  PyObject* capsule;
  double confidence;
  speechwarp_trials* trials;
  (void)self;
  if (!PyArg_ParseTuple(args, "Od", &capsule, &confidence) || !(trials = trials_of(capsule))) return NULL;
  speechwarp_trials_set_confidence(trials, confidence);
  Py_RETURN_NONE;
}

static PyObject* native_syllable_rate(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  return stream ? PyFloat_FromDouble(speechwarp_syllable_rate(stream)) : NULL;
}

static PyObject* native_available(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  return stream ? PyLong_FromLong(speechwarp_available(stream)) : NULL;
}

static PyObject* native_position(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  return stream ? PyLong_FromLongLong(speechwarp_position(stream)) : NULL;
}

static PyObject* native_reset(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  (void)self;
  if (!stream) return NULL;
  speechwarp_reset(stream);
  Py_RETURN_NONE;
}

static PyObject* native_flush(PyObject* self, PyObject* capsule) {
  speechwarp_stream* stream = stream_of(capsule);
  int ok;
  (void)self;
  if (!stream) return NULL;
  Py_BEGIN_ALLOW_THREADS
  ok = speechwarp_flush(stream);
  Py_END_ALLOW_THREADS
  if (!ok) return PyErr_NoMemory();
  Py_RETURN_NONE;
}

/* write(stream, buffer, frames, is_float): the buffer holds frames * channels float32 or int16 samples. */
static PyObject* native_write(PyObject* self, PyObject* args) {
  PyObject* capsule;
  Py_buffer buffer;
  int frames, is_float, ok;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Oy*ip", &capsule, &buffer, &frames, &is_float)) return NULL;
  stream = stream_of(capsule);
  if (!stream) {
    PyBuffer_Release(&buffer);
    return NULL;
  }
  Py_BEGIN_ALLOW_THREADS
  ok = is_float ? speechwarp_write(stream, (const float*)buffer.buf, frames)
                : speechwarp_write_i16(stream, (const int16_t*)buffer.buf, frames);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&buffer);
  if (!ok) return PyErr_NoMemory();
  Py_RETURN_NONE;
}

/* read(stream, buffer, max_frames, is_float) -> frames written to the buffer. */
static PyObject* native_read(PyObject* self, PyObject* args) {
  PyObject* capsule;
  Py_buffer buffer;
  int max_frames, is_float, frames;
  speechwarp_stream* stream;
  (void)self;
  if (!PyArg_ParseTuple(args, "Ow*ip", &capsule, &buffer, &max_frames, &is_float)) return NULL;
  stream = stream_of(capsule);
  if (!stream) {
    PyBuffer_Release(&buffer);
    return NULL;
  }
  Py_BEGIN_ALLOW_THREADS
  frames = is_float ? speechwarp_read(stream, (float*)buffer.buf, max_frames)
                    : speechwarp_read_i16(stream, (int16_t*)buffer.buf, max_frames);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&buffer);
  return PyLong_FromLong(frames);
}

static PyObject* native_version(PyObject* self, PyObject* ignored) {
  (void)self;
  (void)ignored;
  return PyUnicode_FromString(speechwarp_version());
}

static PyMethodDef methods[] = {
    {"create", native_create, METH_VARARGS, NULL},
    {"set_speed", native_set_speed, METH_VARARGS, NULL},
    {"get_speed", native_get_speed, METH_O, NULL},
    {"set_nonlinear", native_set_nonlinear, METH_VARARGS, NULL},
    {"get_nonlinear", native_get_nonlinear, METH_O, NULL},
    {"set_pause_cap", native_set_pause_cap, METH_VARARGS, NULL},
    {"get_pause_cap", native_get_pause_cap, METH_O, NULL},
    {"set_keep_speed", native_set_keep_speed, METH_VARARGS, NULL},
    {"get_keep_speed", native_get_keep_speed, METH_O, NULL},
    {"set_speed_floor", native_set_speed_floor, METH_VARARGS, NULL},
    {"get_speed_floor", native_get_speed_floor, METH_O, NULL},
    {"set_rhythm_gap", native_set_rhythm_gap, METH_VARARGS, NULL},
    {"get_rhythm_gap", native_get_rhythm_gap, METH_O, NULL},
    {"set_rhythm_rate", native_set_rhythm_rate, METH_VARARGS, NULL},
    {"get_rhythm_rate", native_get_rhythm_rate, METH_O, NULL},
    {"syllable_rate", native_syllable_rate, METH_O, NULL},
    {"available", native_available, METH_O, NULL},
    {"position", native_position, METH_O, NULL},
    {"reset", native_reset, METH_O, NULL},
    {"flush", native_flush, METH_O, NULL},
    {"write", native_write, METH_VARARGS, NULL},
    {"read", native_read, METH_VARARGS, NULL},
    {"set_heard_pause", native_set_heard_pause, METH_VARARGS, NULL},
    {"get_heard_pause", native_get_heard_pause, METH_O, NULL},
    {"get_heard_pause_from", native_get_heard_pause_from, METH_O, NULL},
    {"set_floor_blend", native_set_floor_blend, METH_VARARGS, NULL},
    {"get_floor_blend", native_get_floor_blend, METH_O, NULL},
    {"get_floor_blend_from", native_get_floor_blend_from, METH_O, NULL},
    {"get_floor_blend_full", native_get_floor_blend_full, METH_O, NULL},
    {"syllables_create", native_syllables_create, METH_VARARGS, NULL},
    {"syllables_write", native_syllables_write, METH_VARARGS, NULL},
    {"syllables_rate", native_syllables_rate, METH_VARARGS, NULL},
    {"syllables_reset", native_syllables_reset, METH_O, NULL},
    {"trainer_create", native_trainer_create, METH_VARARGS, NULL},
    {"trainer_set_weight", native_trainer_set_weight, METH_VARARGS, NULL},
    {"trainer_get_weight", native_trainer_get_weight, METH_VARARGS, NULL},
    {"trainer_set_param", native_trainer_set_param, METH_VARARGS, NULL},
    {"trainer_get_param", native_trainer_get_param, METH_VARARGS, NULL},
    {"trainer_add_measure", native_trainer_add_measure, METH_VARARGS, NULL},
    {"trainer_test_begin", native_trainer_test_begin, METH_VARARGS, NULL},
    {"trainer_test_rate", native_trainer_test_rate, METH_O, NULL},
    {"trainer_test_done", native_trainer_test_done, METH_O, NULL},
    {"trainer_test_end", native_trainer_test_end, METH_VARARGS, NULL},
    {"trainer_threshold", native_trainer_threshold, METH_O, NULL},
    {"trainer_threshold_low", native_trainer_threshold_low, METH_O, NULL},
    {"trainer_threshold_high", native_trainer_threshold_high, METH_O, NULL},
    {"trainer_session_begin", native_trainer_session_begin, METH_VARARGS, NULL},
    {"trainer_session_rate", native_trainer_session_rate, METH_VARARGS, NULL},
    {"trainer_session_end", native_trainer_session_end, METH_VARARGS, NULL},
    {"trainer_add_retention", native_trainer_add_retention, METH_VARARGS, NULL},
    {"trainer_next_plan", native_trainer_next_plan, METH_O, NULL},
    {"trainer_plan_effect", native_trainer_plan_effect, METH_VARARGS, NULL},
    {"trainer_plan_effect_sd", native_trainer_plan_effect_sd, METH_VARARGS, NULL},
    {"trainer_plan_retention", native_trainer_plan_retention, METH_VARARGS, NULL},
    {"trainer_plan_retention_sd", native_trainer_plan_retention_sd, METH_VARARGS, NULL},
    {"trainer_plan_sessions", native_trainer_plan_sessions, METH_VARARGS, NULL},
    {"trainer_plan_best_probability", native_trainer_plan_best_probability, METH_VARARGS, NULL},
    {"trainer_trend", native_trainer_trend, METH_O, NULL},
    {"trainer_trend_sd", native_trainer_trend_sd, METH_O, NULL},
    {"trials_create", native_trials_create, METH_VARARGS, NULL},
    {"trials_add_setting", native_trials_add_setting, METH_O, NULL},
    {"trials_add_value", native_trials_add_value, METH_VARARGS, NULL},
    {"trials_set_available", native_trials_set_available, METH_VARARGS, NULL},
    {"trials_add", native_trials_add, METH_VARARGS, NULL},
    {"trials_next", native_trials_next, METH_VARARGS, NULL},
    {"trials_won", native_trials_won, METH_VARARGS, NULL},
    {"trials_lost", native_trials_lost, METH_VARARGS, NULL},
    {"trials_tied", native_trials_tied, METH_VARARGS, NULL},
    {"trials_heard", native_trials_heard, METH_VARARGS, NULL},
    {"trials_mean_score", native_trials_mean_score, METH_VARARGS, NULL},
    {"trials_winner", native_trials_winner, METH_VARARGS, NULL},
    {"trials_set_confidence", native_trials_set_confidence, METH_VARARGS, NULL},
    {"version", native_version, METH_NOARGS, NULL},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {PyModuleDef_HEAD_INIT, "speechwarp._native", NULL, -1, methods,
                                    NULL, NULL, NULL, NULL};

PyMODINIT_FUNC PyInit__native(void) {
  PyObject* m = PyModule_Create(&module);
  if (m) {
    PyModule_AddObject(m, "MIN_SPEED", PyFloat_FromDouble(SPEECHWARP_MIN_SPEED));
    PyModule_AddObject(m, "MAX_SPEED", PyFloat_FromDouble(SPEECHWARP_MAX_SPEED));
  }
  return m;
}
