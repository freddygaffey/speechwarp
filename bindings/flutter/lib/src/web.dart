import 'dart:convert';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:math' as math;
import 'dart:typed_data';

import 'common.dart';
import 'wasm_bytes.dart';

// The library compiled to WebAssembly, called through dart:js_interop. The memory handling follows the
// JavaScript binding (bindings/js/src/index.ts): audio is passed through scratch space allocated in the
// library's memory, and a view of that memory is made afresh for every call, because views go stale whenever
// the memory grows.

/// The exports of the WebAssembly instance: the functions of include/speechwarp.h, and malloc and free.
extension type _Exports._(JSObject _) implements JSObject {
  external JSObject get memory;

  @JS('malloc')
  external int malloc(int bytes);
  @JS('free')
  external void free(int pointer);

  @JS('speechwarp_version')
  external int version();
  @JS('speechwarp_create')
  external int create(int sampleRate, int channels);
  @JS('speechwarp_destroy')
  external void destroy(int stream);
  @JS('speechwarp_set_speed')
  external void setSpeed(int stream, double speed);
  @JS('speechwarp_get_speed')
  external double getSpeed(int stream);
  @JS('speechwarp_set_nonlinear')
  external void setNonlinear(int stream, double amount);
  @JS('speechwarp_get_nonlinear')
  external double getNonlinear(int stream);
  @JS('speechwarp_write')
  external int write(int stream, int samples, int frames);
  @JS('speechwarp_write_i16')
  external int writeI16(int stream, int samples, int frames);
  @JS('speechwarp_read')
  external int read(int stream, int samples, int maxFrames);
  @JS('speechwarp_read_i16')
  external int readI16(int stream, int samples, int maxFrames);
  @JS('speechwarp_available')
  external int available(int stream);
  @JS('speechwarp_flush')
  external int flush(int stream);
  @JS('speechwarp_reset')
  external void reset(int stream);
  @JS('speechwarp_position')
  external JSBigInt position(int stream);
  @JS('speechwarp_set_pause_cap')
  external void setPauseCap(int stream, double seconds);
  @JS('speechwarp_get_pause_cap')
  external double getPauseCap(int stream);
  @JS('speechwarp_set_keep_speed')
  external void setKeepSpeed(int stream, int enabled);
  @JS('speechwarp_get_keep_speed')
  external int getKeepSpeed(int stream);
  @JS('speechwarp_set_speed_floor')
  external void setSpeedFloor(int stream, double fraction);
  @JS('speechwarp_get_speed_floor')
  external double getSpeedFloor(int stream);
  @JS('speechwarp_set_rhythm_gap')
  external void setRhythmGap(int stream, double seconds);
  @JS('speechwarp_get_rhythm_gap')
  external double getRhythmGap(int stream);
  @JS('speechwarp_set_rhythm_rate')
  external void setRhythmRate(int stream, double perSecond);
  @JS('speechwarp_get_rhythm_rate')
  external double getRhythmRate(int stream);
  @JS('speechwarp_syllable_rate')
  external double syllableRate(int stream);
  @JS('speechwarp_set_heard_pause')
  external void setHeardPause(int stream, double seconds, double fromSpeed);
  @JS('speechwarp_get_heard_pause')
  external double getHeardPause(int stream);
  @JS('speechwarp_get_heard_pause_from')
  external double getHeardPauseFrom(int stream);
  @JS('speechwarp_set_floor_blend')
  external void setFloorBlend(int stream, double fraction, double fromSpeed, double fullSpeed);
  @JS('speechwarp_get_floor_blend')
  external double getFloorBlend(int stream);
  @JS('speechwarp_get_floor_blend_from')
  external double getFloorBlendFrom(int stream);
  @JS('speechwarp_get_floor_blend_full')
  external double getFloorBlendFull(int stream);
  @JS('speechwarp_syllables_create')
  external int syllablesCreate(int sampleRate, int channels);
  @JS('speechwarp_syllables_destroy')
  external void syllablesDestroy(int counter);
  @JS('speechwarp_syllables_write')
  external int syllablesWrite(int counter, int samples, int frames);
  @JS('speechwarp_syllables_write_i16')
  external int syllablesWriteI16(int counter, int samples, int frames);
  @JS('speechwarp_syllables_rate')
  external double syllablesRate(int counter, double windowSeconds, double minimumSeconds);
  @JS('speechwarp_syllables_reset')
  external void syllablesReset(int counter);
  @JS('speechwarp_trainer_create')
  external int trainerCreate(JSBigInt seed);
  @JS('speechwarp_trainer_destroy')
  external void trainerDestroy(int trainer);
  @JS('speechwarp_trainer_set_weight')
  external void trainerSetWeight(int trainer, int kind, double weight);
  @JS('speechwarp_trainer_get_weight')
  external double trainerGetWeight(int trainer, int kind);
  @JS('speechwarp_trainer_set_param')
  external void trainerSetParam(int trainer, int param, double value);
  @JS('speechwarp_trainer_get_param')
  external double trainerGetParam(int trainer, int param);
  @JS('speechwarp_trainer_add_measure')
  external int trainerAddMeasure(int trainer, int kind, double score, double items, double rate, double time);
  @JS('speechwarp_trainer_test_begin')
  external void trainerTestBegin(int trainer, double priorRate, double time);
  @JS('speechwarp_trainer_test_rate')
  external double trainerTestRate(int trainer);
  @JS('speechwarp_trainer_test_done')
  external int trainerTestDone(int trainer);
  @JS('speechwarp_trainer_test_end')
  external double trainerTestEnd(int trainer, double time);
  @JS('speechwarp_trainer_threshold')
  external double trainerThreshold(int trainer);
  @JS('speechwarp_trainer_threshold_low')
  external double trainerThresholdLow(int trainer);
  @JS('speechwarp_trainer_threshold_high')
  external double trainerThresholdHigh(int trainer);
  @JS('speechwarp_trainer_session_begin')
  external void trainerSessionBegin(int trainer, int plan, double time);
  @JS('speechwarp_trainer_session_rate')
  external double trainerSessionRate(int trainer, double time);
  @JS('speechwarp_trainer_session_end')
  external int trainerSessionEnd(int trainer, double listeningHours, double time);
  @JS('speechwarp_trainer_add_retention')
  external int trainerAddRetention(
      int trainer, int session, double score, double items, double delaySeconds, double time);
  @JS('speechwarp_trainer_next_plan')
  external int trainerNextPlan(int trainer);
  @JS('speechwarp_trainer_plan_effect')
  external double trainerPlanEffect(int trainer, int plan);
  @JS('speechwarp_trainer_plan_effect_sd')
  external double trainerPlanEffectSd(int trainer, int plan);
  @JS('speechwarp_trainer_plan_retention')
  external double trainerPlanRetention(int trainer, int plan);
  @JS('speechwarp_trainer_plan_retention_sd')
  external double trainerPlanRetentionSd(int trainer, int plan);
  @JS('speechwarp_trainer_plan_sessions')
  external int trainerPlanSessions(int trainer, int plan);
  @JS('speechwarp_trainer_plan_best_probability')
  external double trainerPlanBestProbability(int trainer, int plan);
  @JS('speechwarp_trainer_trend')
  external double trainerTrend(int trainer);
  @JS('speechwarp_trainer_trend_sd')
  external double trainerTrendSd(int trainer);
  @JS('speechwarp_trials_create')
  external int trialsCreate(JSBigInt seed);
  @JS('speechwarp_trials_destroy')
  external void trialsDestroy(int trials);
  @JS('speechwarp_trials_add_setting')
  external int trialsAddSetting(int trials);
  @JS('speechwarp_trials_add_value')
  external int trialsAddValue(int trials, int setting, double value);
  @JS('speechwarp_trials_set_available')
  external void trialsSetAvailable(int trials, int setting, int available);
  @JS('speechwarp_trials_add')
  external int trialsAdd(int trials, int setting, double speed, double firstValue, double secondValue,
      double firstScore, double secondScore, int preferred);
  @JS('speechwarp_trials_next')
  external int trialsNext(int trials, double speed);
  @JS('speechwarp_trials_next_first')
  external double trialsNextFirst(int trials);
  @JS('speechwarp_trials_next_second')
  external double trialsNextSecond(int trials);
  @JS('speechwarp_trials_won')
  external int trialsWon(int trials, int setting, double speed, int value);
  @JS('speechwarp_trials_lost')
  external int trialsLost(int trials, int setting, double speed, int value);
  @JS('speechwarp_trials_tied')
  external int trialsTied(int trials, int setting, double speed, int value);
  @JS('speechwarp_trials_heard')
  external int trialsHeard(int trials, int setting, double speed, int value);
  @JS('speechwarp_trials_mean_score')
  external double trialsMeanScore(int trials, int setting, double speed, int value);
  @JS('speechwarp_trials_winner')
  external int trialsWinner(int trials, int setting, double speed);
  @JS('speechwarp_trials_set_confidence')
  external void trialsSetConfidence(int trials, double confidence);
  @JS('speechwarp_score_words')
  external double scoreWords(int reference, int heard, int counts);
}

/// A `WebAssembly.Memory`. Its buffer is replaced whenever the memory grows.
extension type _Memory._(JSObject _) implements JSObject {
  external JSArrayBuffer get buffer;
}

// Typed arrays laid over the library's memory.

@JS('Float32Array')
extension type _Floats._(JSObject _) implements JSObject {
  external _Floats(JSArrayBuffer buffer, int byteOffset, int length);
  @JS('set')
  external void setAll(JSFloat32Array source);
  external JSFloat32Array slice(int start, int end);
}

@JS('Int16Array')
extension type _Shorts._(JSObject _) implements JSObject {
  external _Shorts(JSArrayBuffer buffer, int byteOffset, int length);
  @JS('set')
  external void setAll(JSInt16Array source);
  external JSInt16Array slice(int start, int end);
}

@JS('Uint8Array')
extension type _Bytes._(JSObject _) implements JSObject {
  external _Bytes(JSArrayBuffer buffer, int byteOffset, int length);
  @JS('set')
  external void setAll(JSUint8Array source);
  external JSUint8Array slice(int start, int end);
}

@JS('Int32Array')
extension type _Ints._(JSObject _) implements JSObject {
  external _Ints(JSArrayBuffer buffer, int byteOffset, int length);
  external JSInt32Array slice(int start, int end);
}

@JS('WebAssembly.compile')
external JSPromise<JSObject> _compile(JSUint8Array bytes);
@JS('WebAssembly.instantiate')
external JSPromise<JSObject> _instantiate(JSObject module, JSObject imports);
@JS('WebAssembly.Module.imports')
external JSArray<JSObject> _moduleImports(JSObject module);
@JS('BigInt')
external JSBigInt _bigInt(JSAny value);
@JS('BigInt.asUintN')
external JSBigInt _asUintN(int bits, JSBigInt value);
@JS('Number')
external double _numberOf(JSAny value);

_Exports? _exports;
String _version = '';
Future<void>? _loading;

/// The loaded library; a [StateError] if [Speechwarp.initialize] has not completed.
_Exports get _ready {
  final exports = _exports;
  if (exports == null) {
    throw StateError('speechwarp is not ready on the web: await Speechwarp.initialize() before using it');
  }
  return exports;
}

/// The library asks its host for a few things it never uses in earnest, such as somewhere to print.
JSObject _importsFor(JSObject module) {
  final imports = JSObject();
  for (final entry in _moduleImports(module).toDart) {
    final from = entry.getProperty<JSString>('module'.toJS).toDart;
    final name = entry.getProperty<JSString>('name'.toJS).toDart;
    final kind = entry.getProperty<JSString>('kind'.toJS).toDart;
    if (kind != 'function') throw StateError('speechwarp: unexpected WebAssembly import $from.$name');
    final JSObject namespace;
    if (imports.has(from)) {
      namespace = imports.getProperty<JSObject>(from.toJS);
    } else {
      namespace = JSObject();
      imports.setProperty(from.toJS, namespace);
    }
    namespace.setProperty(name.toJS, (() => 0.toJS).toJS);
  }
  return imports;
}

Future<void> _load() async {
  if (wasmBase64.isEmpty) {
    throw StateError('speechwarp: this copy of the package has no WebAssembly in it. Run scripts/build_web.sh '
        '(it needs Emscripten) in the package, or use the package as published on pub.dev, which includes it.');
  }
  final module = await _compile(base64Decode(wasmBase64).toJS).toDart;
  final instance = await _instantiate(module, _importsFor(module)).toDart;
  final exports = _Exports._(instance.getProperty<JSObject>('exports'.toJS));
  if (exports.has('_initialize')) exports.callMethod<JSAny?>('_initialize'.toJS);
  _version = _readText(exports, exports.version());
  _exports = exports;
}

/// Reads the zero-ended text at [pointer] in the library's memory.
String _readText(_Exports exports, int pointer) {
  final memory = _Memory._(exports.memory).buffer;
  final length = (memory.toDart.lengthInBytes - pointer).clamp(0, 64);
  final bytes = _Bytes(memory, pointer, length).slice(0, length).toDart;
  final end = bytes.indexOf(0);
  return ascii.decode(end < 0 ? bytes : Uint8List.sublistView(bytes, 0, end));
}

/// A seed as the unsigned 64-bit number the library takes; a negative [seed] stands for its two's complement.
/// It goes through a string so that a whole 64-bit integer survives wherever the platform has one.
JSBigInt _seed(int seed) => _asUintN(64, _bigInt(seed.toString().toJS));

/// Gets the library ready to use.
///
/// Call `await Speechwarp.initialize()` once, before anything else, in an app that runs on the web. On the web
/// the library is WebAssembly, which has to be compiled and started before it can be used, and that can only
/// be waited for. On Android, iOS, macOS, Linux and Windows the library is native code and this completes at
/// once, so the same code runs everywhere. Calling it again does nothing.
abstract final class Speechwarp {
  /// Gets the library ready; see [Speechwarp]. Completes at once on every platform but the web.
  ///
  /// On the web it compiles and starts the WebAssembly the first time. Calls made while that is happening
  /// share it, and calls after it return at once. If it fails, the error is thrown and a later call tries
  /// again. Using a class of this library before this has completed throws a [StateError].
  static Future<void> initialize() {
    if (_exports != null) return Future<void>.value();
    return _loading ??= _load().catchError((Object error, StackTrace stack) {
      _loading = null;
      Error.throwWithStackTrace(error, stack);
    });
  }

  /// Whether [initialize] has completed. Always true except on the web.
  static bool get isInitialized => _exports != null;
}

/// Scratch space in the library's memory through which audio is passed, grown as needed and kept between calls.
final class _Scratch {
  _Scratch(this._exports);

  final _Exports _exports;
  int _pointer = 0;
  int _bytes = 0;

  JSArrayBuffer get _buffer => _Memory._(_exports.memory).buffer;

  /// The address of space for [bytes] bytes, valid until the next call to this or to [free].
  int room(int bytes) {
    if (bytes > _bytes) {
      _exports.free(_pointer);
      _bytes = math.max(bytes, 2 * _bytes);
      _pointer = _exports.malloc(_bytes);
      if (_pointer == 0) {
        _bytes = 0;
        throw const OutOfMemoryError();
      }
    }
    return _pointer;
  }

  void free() {
    _exports.free(_pointer);
    _pointer = 0;
    _bytes = 0;
  }

  /// Copies [samples] into the scratch space and returns its address.
  int putFloats(Float32List samples) {
    final pointer = room(samples.length * 4);
    _Floats(_buffer, pointer, samples.length).setAll(samples.toJS);
    return pointer;
  }

  int putShorts(Int16List samples) {
    final pointer = room(samples.length * 2);
    _Shorts(_buffer, pointer, samples.length).setAll(samples.toJS);
    return pointer;
  }

  /// A copy of [count] samples that the library has written at [pointer].
  Float32List takeFloats(int pointer, int count) => _Floats(_buffer, pointer, count).slice(0, count).toDart;

  Int16List takeShorts(int pointer, int count) => _Shorts(_buffer, pointer, count).slice(0, count).toDart;
}

/// What a Dart object owns in the library: a pointer and scratch space. Kept apart from the object so that it
/// can be released when the object is dropped without having been closed.
final class _Handle {
  _Handle(this.exports, this.pointer, this._destroy) : scratch = _Scratch(exports);

  final _Exports exports;
  final _Scratch scratch;
  final void Function(int) _destroy;
  int pointer;

  bool get isClosed => pointer == 0;

  void release() {
    if (pointer == 0) return;
    _destroy(pointer);
    pointer = 0;
    scratch.free();
  }
}

// Frees the memory of an object that is dropped without close() being called.
final Finalizer<_Handle> _finalizer = Finalizer<_Handle>((handle) => handle.release());

/// Speeds up speech. Write audio in, read the faster audio out.
///
/// A stream belongs to the isolate that created it. [close] it when done.
class SpeechwarpStream {
  /// The slowest speed that can be set.
  static const double minSpeed = 0.05;

  /// The fastest speed that can be set.
  static const double maxSpeed = 20;

  /// The version of the C library, such as "0.3.7".
  static String get libraryVersion {
    _ready; // throws unless the library is loaded
    return _version;
  }

  /// Samples per second.
  final int sampleRate;

  /// Samples in a frame.
  final int channels;

  final _Handle _handle;

  /// Creates a stream at speed 1 with nonlinear speed-up on.
  ///
  /// [sampleRate] is 4000 to 384000 and [channels] 1 to 32.
  SpeechwarpStream(this.sampleRate, {this.channels = 1}) : _handle = _start(sampleRate, channels) {
    _finalizer.attach(this, _handle, detach: this);
  }

  static _Handle _start(int sampleRate, int channels) {
    RangeError.checkValueInInterval(sampleRate, 4000, 384000, 'sampleRate');
    RangeError.checkValueInInterval(channels, 1, 32, 'channels');
    final exports = _ready;
    final pointer = exports.create(sampleRate, channels);
    if (pointer == 0) throw const OutOfMemoryError();
    return _Handle(exports, pointer, (pointer) => exports.destroy(pointer));
  }

  _Exports get _library => _handle.exports;

  /// Overall speed: 2 plays twice as fast. Clamped to [minSpeed]..[maxSpeed].
  ///
  /// Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
  /// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
  /// result within a few percent.
  double get speed => _library.getSpeed(_open());
  set speed(double value) {
    if (!(value > 0)) throw ArgumentError.value(value, 'speed', 'must be greater than zero');
    _library.setSpeed(_open(), value);
  }

  /// How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
  /// pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
  double get nonlinear => _library.getNonlinear(_open());
  set nonlinear(double value) {
    if (value.isNaN) throw ArgumentError.value(value, 'nonlinear', 'must be a number');
    _library.setNonlinear(_open(), value);
  }

  // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
  // docs/how-it-works.md.

  /// Shortens every pause to at most this many seconds of input before speeding up, so that the speed is spent
  /// on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. [position] counts the
  /// frames left out.
  double get pauseCap => _library.getPauseCap(_open());
  set pauseCap(double value) => _library.setPauseCap(_open(), checkedNumber(value, 'pauseCap'));

  /// While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses is
  /// spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
  /// When false, trimmed pauses make playback faster than [speed].
  bool get keepSpeed => _library.getKeepSpeed(_open()) != 0;
  set keepSpeed(bool value) => _library.setKeepSpeed(_open(), value ? 1 : 0);

  /// No 10 ms block of speech plays slower than this fraction of [speed]: at 8x, 0.5 keeps every block at 4x or
  /// more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with nonlinear
  /// speed-up above 1x.
  double get speedFloor => _library.getSpeedFloor(_open());
  set speedFloor(double value) => _library.setSpeedFloor(_open(), checkedNumber(value, 'speedFloor'));

  /// Seconds of silence put into the output [rhythmRate] times a second, at the quietest point nearby, which can
  /// help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06. Allowed: 0,
  /// or 0.005 to 0.2. [position] holds still during a gap.
  double get rhythmGap => _library.getRhythmGap(_open());
  set rhythmGap(double value) => _library.setRhythmGap(_open(), checkedNumber(value, 'rhythmGap'));

  /// Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8.
  double get rhythmRate => _library.getRhythmRate(_open());
  set rhythmRate(double value) {
    if (!(value > 0)) throw ArgumentError.value(value, 'rhythmRate', 'must be greater than zero');
    _library.setRhythmRate(_open(), value);
  }

  // Options that follow the speed. See docs/how-it-works.md.

  /// Keeps each pause about [seconds] long in the output: the pause cap in force is [seconds] times the current
  /// speed, clamped to 0.03 to 0.4 s of input, and is applied whenever [speed] changes. Below [fromSpeed] pauses
  /// are left alone. [seconds]: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to 0.4.
  /// [fromSpeed]: 1 to 20. Sensible: 0.015 to 0.06 from 3x. Setting [pauseCap] turns the rule off.
  void setHeardPause(double seconds, double fromSpeed) =>
      _library.setHeardPause(_open(), checkedNumber(seconds, 'seconds'), checkedNumber(fromSpeed, 'fromSpeed'));

  /// The `seconds` last given to [setHeardPause]; 0 while the rule is off.
  double get heardPause => _library.getHeardPause(_open());

  /// The `fromSpeed` last given to [setHeardPause].
  double get heardPauseFrom => _library.getHeardPauseFrom(_open());

  /// The speed floor in force is 0 below [fromSpeed], rises linearly to [fraction] at [fullSpeed] and stays there
  /// above it, so that a speed ramp never changes the sound in a jump. [fraction]: 0 turns the rule off (and the
  /// floor with it); otherwise up to 1. Speeds 1 to 20; if [fullSpeed] is not above [fromSpeed] the floor steps
  /// to [fraction] at [fromSpeed]. Sensible: 0.5 from 4x, full at 6x. Setting [speedFloor] turns the rule off.
  void setFloorBlend(double fraction, double fromSpeed, double fullSpeed) => _library.setFloorBlend(
      _open(),
      checkedNumber(fraction, 'fraction'),
      checkedNumber(fromSpeed, 'fromSpeed'),
      checkedNumber(fullSpeed, 'fullSpeed'));

  /// The `fraction` last given to [setFloorBlend]; 0 while the rule is off.
  double get floorBlend => _library.getFloorBlend(_open());

  /// The `fromSpeed` last given to [setFloorBlend].
  double get floorBlendFrom => _library.getFloorBlendFrom(_open());

  /// The `fullSpeed` last given to [setFloorBlend].
  double get floorBlendFull => _library.getFloorBlendFull(_open());

  /// Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
  /// been written since creation or [reset]. Multiply by the speed for the rate heard. An estimate, typically
  /// within about 10%.
  double? get syllableRate {
    final rate = _library.syllableRate(_open());
    return rate < 0 ? null : rate;
  }

  /// Frames of output ready to read.
  int get framesAvailable => _library.available(_open());

  /// The input frame, counted from creation or the last [reset], that the next output frame to be read was
  /// made from. This is how a player maps what is being heard back to a place in the source.
  ///
  /// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
  /// [flush] has been read it equals the number of frames written.
  int get position => _numberOf(_library.position(_open())).toInt();

  /// Adds interleaved input. The output does not depend on how the input is divided between calls.
  void write(Float32List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final pointer = _handle.scratch.putFloats(samples);
    if (_library.write(_handle.pointer, pointer, frames) == 0) throw const OutOfMemoryError();
  }

  /// Adds interleaved 16-bit input.
  void writeInt16(Int16List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final pointer = _handle.scratch.putShorts(samples);
    if (_library.writeI16(_handle.pointer, pointer, frames) == 0) throw const OutOfMemoryError();
  }

  /// Takes processed output: everything that is ready, or at most [maxFrames] frames of it.
  ///
  /// The list may be empty: output lags input by a short look-ahead.
  Float32List read([int? maxFrames]) {
    final frames = _framesToRead(maxFrames);
    if (frames == 0) return Float32List(0);
    final pointer = _handle.scratch.room(frames * channels * 4);
    final got = _library.read(_handle.pointer, pointer, frames);
    return _handle.scratch.takeFloats(pointer, got * channels);
  }

  /// Takes processed output as 16-bit samples. See [read].
  Int16List readInt16([int? maxFrames]) {
    final frames = _framesToRead(maxFrames);
    if (frames == 0) return Int16List(0);
    final pointer = _handle.scratch.room(frames * channels * 2);
    final got = _library.readI16(_handle.pointer, pointer, frames);
    return _handle.scratch.takeShorts(pointer, got * channels);
  }

  /// Processes everything written so far, at the end of the input. Read afterwards to get the rest. Writing
  /// more starts a new stretch of audio, and [position] carries on counting.
  void flush() {
    if (_library.flush(_open()) == 0) throw const OutOfMemoryError();
  }

  /// Discards all buffered input and output, keeping the speed and nonlinear settings, and starts [position]
  /// again from zero. Use after seeking.
  void reset() => _library.reset(_open());

  /// Frees the stream. Using the stream afterwards throws [StateError].
  void close() {
    if (_handle.isClosed) return;
    _finalizer.detach(this);
    _handle.release();
  }

  int _open() {
    if (_handle.isClosed) throw StateError('the stream is closed');
    return _handle.pointer;
  }

  int _wholeFrames(int samples) {
    _open();
    if (samples % channels != 0) {
      throw ArgumentError('$samples samples is not a whole number of $channels-channel frames');
    }
    return samples ~/ channels;
  }

  int _framesToRead(int? maxFrames) {
    final ready = framesAvailable;
    if (maxFrames == null) return ready;
    if (maxFrames < 0) throw RangeError.value(maxFrames, 'maxFrames', 'must not be negative');
    return maxFrames < ready ? maxFrames : ready;
  }
}

/// The syllable counter of a stream, on its own: for audio that does not go through a stream (a player using
/// some other speed-up, or measuring a file). A counter given the same input as a stream reports the same rate as
/// [SpeechwarpStream.syllableRate]. [close] it when done.
class SyllableCounter {
  /// Samples per second.
  final int sampleRate;

  /// Samples in a frame.
  final int channels;

  final _Handle _handle;

  /// Creates a counter. [sampleRate] is 4000 to 384000 and [channels] 1 to 32.
  SyllableCounter(this.sampleRate, {this.channels = 1}) : _handle = _start(sampleRate, channels) {
    _finalizer.attach(this, _handle, detach: this);
  }

  static _Handle _start(int sampleRate, int channels) {
    RangeError.checkValueInInterval(sampleRate, 4000, 384000, 'sampleRate');
    RangeError.checkValueInInterval(channels, 1, 32, 'channels');
    final exports = _ready;
    final pointer = exports.syllablesCreate(sampleRate, channels);
    if (pointer == 0) throw const OutOfMemoryError();
    return _Handle(exports, pointer, (pointer) => exports.syllablesDestroy(pointer));
  }

  /// Adds interleaved samples in the range -1 to 1.
  void write(Float32List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final pointer = _handle.scratch.putFloats(samples);
    if (_handle.exports.syllablesWrite(_handle.pointer, pointer, frames) == 0) {
      throw StateError('the samples could not be counted');
    }
  }

  /// Adds interleaved 16-bit samples.
  void writeInt16(Int16List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final pointer = _handle.scratch.putShorts(samples);
    if (_handle.exports.syllablesWriteI16(_handle.pointer, pointer, frames) == 0) {
      throw StateError('the samples could not be counted');
    }
  }

  /// Syllables a second over the last [windowSeconds] written (or all of it, if less); null until
  /// [minimumSeconds] have been written. The window is clamped to 1 to 120 s, the minimum to 0 to the window.
  double? rate({double windowSeconds = 60, double minimumSeconds = 10}) {
    final value = _handle.exports.syllablesRate(
        _open(), checkedNumber(windowSeconds, 'windowSeconds'), checkedNumber(minimumSeconds, 'minimumSeconds'));
    return value < 0 ? null : value;
  }

  /// Forgets everything written.
  void reset() => _handle.exports.syllablesReset(_open());

  /// Frees the counter. Using it afterwards throws [StateError].
  void close() {
    if (_handle.isClosed) return;
    _finalizer.detach(this);
    _handle.release();
  }

  int _open() {
    if (_handle.isClosed) throw StateError('the syllable counter is closed');
    return _handle.pointer;
  }

  int _wholeFrames(int samples) {
    _open();
    if (samples % channels != 0) {
      throw ArgumentError('$samples samples is not a whole number of $channels-channel frames');
    }
    return samples ~/ channels;
  }
}

/// Training a listener to follow faster speech: pure logic with no audio, clock or storage. Scores, rates and
/// timestamps go in as plain numbers; rates and plans come out. Deterministic: two trainers with the same seed
/// given the same calls give the same answers, so keep a log of calls and replay it to restore state.
///
/// The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times are
/// seconds on any clock, and only differences are used. NaN arguments are ignored. [close] it when done.
class ListenerTrainer {
  final _Handle _handle;

  /// Creates a trainer. [seed] seeds its random choices; it is an unsigned 64-bit number, so a negative [int]
  /// stands for its two's complement.
  ListenerTrainer({int seed = 0}) : _handle = _start(seed) {
    _finalizer.attach(this, _handle, detach: this);
  }

  static _Handle _start(int seed) {
    final exports = _ready;
    final pointer = exports.trainerCreate(_seed(seed));
    if (pointer == 0) throw const OutOfMemoryError();
    return _Handle(exports, pointer, (pointer) => exports.trainerDestroy(pointer));
  }

  _Exports get _library => _handle.exports;

  /// How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification 1,
  /// retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
  void setWeight(TrainerMeasure kind, double weight) => _library.trainerSetWeight(_open(), kind.value, weight);

  /// The weight of a kind of measure; see [setWeight].
  double getWeight(TrainerMeasure kind) => _library.trainerGetWeight(_open(), kind.value);

  /// Sets a tunable number; see [TrainerParam] for the defaults.
  void setParam(TrainerParam param, double value) => _library.trainerSetParam(_open(), param.value, value);

  /// A tunable number.
  double getParam(TrainerParam param) => _library.trainerGetParam(_open(), param.value);

  /// Records a score: [kind] (not retention), [score] 0..1, from [items] items (for a sentence repeated back, the
  /// number of words scored; for verification, the number of questions; for a rating, 1), heard at [rate]
  /// syllables a second, at [time]. During a threshold test it updates the estimate; during a session it is an
  /// in-session check, and the tracking plan reacts to it. Returns false if an argument is invalid.
  bool addMeasure(TrainerMeasure kind, double score, double items, double rate, double time) =>
      _library.trainerAddMeasure(_open(), kind.value, score, items, rate, time) != 0;

  /// Starts a threshold test: estimates the rate understood [TrainerParam.target] (75%) of the time, by the psi
  /// method. The prior is log-normal around [priorRate] or, if that is 0, around the last estimate, or failing
  /// that around 10 syllables a second, within 3 to 60.
  void testBegin(double priorRate, double time) => _library.trainerTestBegin(_open(), priorRate, time);

  /// The rate to present next.
  double testRate() => _library.trainerTestRate(_open());

  /// True once the 95% interval is narrower than [TrainerParam.testPrecision] (at least 8 presentations) or
  /// [TrainerParam.testMax] presentations have been scored; false otherwise or if no test is running.
  bool get testDone => _library.trainerTestDone(_open()) != 0;

  /// Finishes the test; the estimate becomes the current threshold. Returns it (0 if no test was running).
  double testEnd(double time) => _library.trainerTestEnd(_open(), time);

  /// The current estimate (posterior median): of the running test, else of the last one finished; 0 if none.
  double get threshold => _library.trainerThreshold(_open());

  /// The low end of the 95% interval of the estimate.
  double get thresholdLow => _library.trainerThresholdLow(_open());

  /// The high end of the 95% interval of the estimate.
  double get thresholdHigh => _library.trainerThresholdHigh(_open());

  /// Begins a session under [plan].
  void sessionBegin(TrainerPlan plan, double time) => _library.trainerSessionBegin(_open(), plan.value, time);

  /// The rate to play at now, under the session's plan, from the threshold at [sessionBegin]. 0 if no session.
  double sessionRate(double time) => _library.trainerSessionRate(_open(), time);

  /// Ends the session after [listeningHours] of listening in it. It is recorded for comparing plans if a
  /// threshold test ended after it began. Returns the session's number (0, 1, ...) for [addRetention], or -1.
  int sessionEnd(double listeningHours, double time) => _library.trainerSessionEnd(_open(), listeningHours, time);

  /// Retention for a recorded [session]: [score] 0..1 from [items] items, answered [delaySeconds] after it ended.
  /// Returns false if an argument is invalid.
  bool addRetention(int session, double score, double items, double delaySeconds, double time) =>
      _library.trainerAddRetention(_open(), session, score, items, delaySeconds, time) != 0;

  /// The plan to run next: a Thompson draw (advances the random source).
  TrainerPlan nextPlan() => TrainerPlan.values[_library.trainerNextPlan(_open())];

  /// A plan's estimated threshold gain an hour now, as a fraction: 0.01 is 1% an hour.
  double planEffect(TrainerPlan plan) => _library.trainerPlanEffect(_open(), plan.value);

  /// The standard deviation of [planEffect].
  double planEffectSd(TrainerPlan plan) => _library.trainerPlanEffectSd(_open(), plan.value);

  /// A plan's retention; NaN without data.
  double planRetention(TrainerPlan plan) => _library.trainerPlanRetention(_open(), plan.value);

  /// The standard deviation of [planRetention]; NaN without data.
  double planRetentionSd(TrainerPlan plan) => _library.trainerPlanRetentionSd(_open(), plan.value);

  /// Sessions recorded under a plan.
  int planSessions(TrainerPlan plan) => _library.trainerPlanSessions(_open(), plan.value);

  /// The probability that a plan is the best by utility (does not advance the random source).
  double planBestProbability(TrainerPlan plan) => _library.trainerPlanBestProbability(_open(), plan.value);

  /// H, the hours of listening by which gains have halved (1000 standing for "not slowing").
  double get trend => _library.trainerTrend(_open());

  /// The uncertainty of [trend] as a standard deviation of ln H.
  double get trendSd => _library.trainerTrendSd(_open());

  /// Frees the trainer. Using it afterwards throws [StateError].
  void close() {
    if (_handle.isClosed) return;
    _finalizer.detach(this);
    _handle.release();
  }

  int _open() {
    if (_handle.isClosed) throw StateError('the trainer is closed');
    return _handle.pointer;
  }
}

/// Designing the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
/// values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
/// caller names the settings; here they are numbers. Deterministic for a given seed. [close] it when done.
class BlindTrials {
  final _Handle _handle;

  /// Creates a designer. [seed] seeds its random choices; it is an unsigned 64-bit number, so a negative [int]
  /// stands for its two's complement.
  BlindTrials({int seed = 0}) : _handle = _start(seed) {
    _finalizer.attach(this, _handle, detach: this);
  }

  static _Handle _start(int seed) {
    final exports = _ready;
    final pointer = exports.trialsCreate(_seed(seed));
    if (pointer == 0) throw const OutOfMemoryError();
    return _Handle(exports, pointer, (pointer) => exports.trialsDestroy(pointer));
  }

  _Exports get _library => _handle.exports;

  /// Adds a setting; returns its number (0, 1, ...), or -1.
  int addSetting() => _library.trialsAddSetting(_open());

  /// Adds a value to compare; returns its number within the setting, or -1 (duplicate, bad setting).
  int addValue(int setting, double value) => _library.trialsAddValue(_open(), setting, value);

  /// Leaves a setting out of [next] while false (say, when its method is not available). Default true.
  void setAvailable(int setting, bool available) => _library.trialsSetAvailable(_open(), setting, available ? 1 : 0);

  /// Records a trial at [speed]: the two values in the order heard, each one's score 0..1, and [preferred]: -1 the
  /// first, 1 the second, 0 neither. Values must be ones added. Returns false if an argument is invalid.
  bool add(int setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore,
          int preferred) =>
      _library.trialsAdd(_open(), setting, speed, firstValue, secondValue, firstScore, secondScore, preferred) != 0;

  /// Chooses the next trial at [speed]: the available setting with the fewest trials in its band (ties at random),
  /// its pair of values compared least (ties at random), in random order. Null if no setting has two values.
  ({int setting, double first, double second})? next(double speed) {
    final trials = _open();
    final setting = _library.trialsNext(trials, speed);
    if (setting < 0) return null;
    return (setting: setting, first: _library.trialsNextFirst(trials), second: _library.trialsNextSecond(trials));
  }

  /// Comparisons won by one value of a setting in the band of [speed].
  int won(int setting, double speed, int value) => _library.trialsWon(_open(), setting, speed, value);

  /// Comparisons lost by one value of a setting in the band of [speed].
  int lost(int setting, double speed, int value) => _library.trialsLost(_open(), setting, speed, value);

  /// Comparisons tied by one value of a setting in the band of [speed].
  int tied(int setting, double speed, int value) => _library.trialsTied(_open(), setting, speed, value);

  /// Trials one value of a setting was heard in, in the band of [speed].
  int heard(int setting, double speed, int value) => _library.trialsHeard(_open(), setting, speed, value);

  /// The mean score of one value in the band of [speed]; null if never heard.
  double? meanScore(int setting, double speed, int value) {
    final score = _library.trialsMeanScore(_open(), setting, speed, value);
    return score.isNaN ? null : score;
  }

  /// The value with a reliable win in that band, or null. A value wins when it has been heard in at least 5
  /// trials, has met every other value in at least 3, and against each the Bayes factor for "preferred" over "no
  /// preference" is at least 1 / (1 - confidence): 20 at the default 0.95.
  int? winner(int setting, double speed) {
    final value = _library.trialsWinner(_open(), setting, speed);
    return value < 0 ? null : value;
  }

  /// The confidence [winner] demands, 0 to 1; default 0.95.
  void setConfidence(double confidence) => _library.trialsSetConfidence(_open(), confidence);

  /// Frees the trials. Using them afterwards throws [StateError].
  void close() {
    if (_handle.isClosed) return;
    _finalizer.detach(this);
    _handle.release();
  }

  int _open() {
    if (_handle.isClosed) throw StateError('the trials are closed');
    return _handle.pointer;
  }
}

/// Scores [heard] (what the listener said or typed) against [reference] (the sentence played).
///
/// Both are split into words the same way: letters folded to lower case (ASCII and the Latin letters),
/// punctuation dropped, an apostrophe inside a word kept (' and U+2019 alike, so "Don't" matches "don’t"), and
/// numbers left as digits ("3" and "three" differ). The two are aligned by word-level edit distance; among the
/// cheapest alignments the one with the most words right is taken. The rules in full are at
/// speechwarp_score_words in include/speechwarp.h. Dart has no Unicode normaliser built in, so give both in the
/// same form (NFC, as most text already is): a decomposed "e" plus accent does not match "é".
///
/// Throws [OutOfMemoryError] if the library could not allocate its working space.
WordScore scoreWords(String reference, String heard) {
  final exports = _ready;
  final referenceBytes = utf8.encode(reference);
  final heardBytes = utf8.encode(heard);
  final size = referenceBytes.length + heardBytes.length + 2;
  final pointer = exports.malloc(size + 16);
  if (pointer == 0) throw const OutOfMemoryError();
  try {
    final heardAt = pointer + referenceBytes.length + 1;
    final countsAt = (pointer + size + 3) & ~3;
    final text = Uint8List(size)
      ..setAll(0, referenceBytes)
      ..setAll(referenceBytes.length + 1, heardBytes);
    final buffer = _Memory._(exports.memory).buffer;
    _Bytes(buffer, pointer, size).setAll(text.toJS);
    final share = exports.scoreWords(pointer, heardAt, countsAt);
    if (share < 0) throw const OutOfMemoryError();
    final counts = _Ints(_Memory._(exports.memory).buffer, countsAt, 4).slice(0, 4).toDart;
    return WordScore(share, counts[0], counts[1], counts[2], counts[3]);
  } finally {
    exports.free(pointer);
  }
}
