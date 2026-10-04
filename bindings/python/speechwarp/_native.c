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
