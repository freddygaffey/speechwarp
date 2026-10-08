/// Nonlinear speed-up for speech: listen faster and still follow it.
///
/// ```dart
/// final stream = SpeechwarpStream(44100, channels: 2)..speed = 3;
/// stream.write(samples);             // interleaved Float32List
/// play(stream.read());               // everything that is ready
/// stream.flush();                    // at the end of the input
/// play(stream.read());
/// stream.close();
/// ```
///
/// Samples are interleaved: a frame is one sample per channel. Lists are measured in samples, as lists are;
/// `maxFrames`, [SpeechwarpStream.framesAvailable] and [SpeechwarpStream.position] are in frames.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

final DynamicLibrary _library = () {
  // Lets tests and command-line programs, which have no app bundle, say where the library is.
  final override = Platform.environment['SPEECHWARP_LIBRARY'];
  if (override != null && override.isNotEmpty) return DynamicLibrary.open(override);
  if (Platform.isMacOS || Platform.isIOS) return DynamicLibrary.open('speechwarp.framework/speechwarp');
  if (Platform.isAndroid || Platform.isLinux) return DynamicLibrary.open('libspeechwarp.so');
  if (Platform.isWindows) return DynamicLibrary.open('speechwarp.dll');
  throw UnsupportedError('speechwarp does not support ${Platform.operatingSystem}');
}();

final _version = _library.lookupFunction<Pointer<Utf8> Function(), Pointer<Utf8> Function()>('speechwarp_version');
final _create = _library
    .lookupFunction<Pointer<Void> Function(Int32, Int32), Pointer<Void> Function(int, int)>('speechwarp_create');
final _destroyPointer = _library.lookup<NativeFunction<Void Function(Pointer<Void>)>>('speechwarp_destroy');
final _destroy = _destroyPointer.asFunction<void Function(Pointer<Void>)>();
final _setSpeed = _library
    .lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>('speechwarp_set_speed');
final _getSpeed =
    _library.lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_speed');
final _setNonlinear =
    _library.lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>(
        'speechwarp_set_nonlinear');
final _getNonlinear =
    _library.lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_nonlinear');
final _setPauseCap = _library.lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>(
    'speechwarp_set_pause_cap');
final _getPauseCap =
    _library.lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_pause_cap');
final _setKeepSpeed = _library.lookupFunction<Void Function(Pointer<Void>, Int32), void Function(Pointer<Void>, int)>(
    'speechwarp_set_keep_speed');
final _getKeepSpeed =
    _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>('speechwarp_get_keep_speed');
final _setSpeedFloor =
    _library.lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>(
        'speechwarp_set_speed_floor');
final _getSpeedFloor = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_speed_floor');
final _setRhythmGap =
    _library.lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>(
        'speechwarp_set_rhythm_gap');
final _getRhythmGap =
    _library.lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_rhythm_gap');
final _setRhythmRate =
    _library.lookupFunction<Void Function(Pointer<Void>, Float), void Function(Pointer<Void>, double)>(
        'speechwarp_set_rhythm_rate');
final _getRhythmRate = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_rhythm_rate');
final _syllableRate =
    _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_syllable_rate');
final _write = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Float>, Int32),
    int Function(Pointer<Void>, Pointer<Float>, int)>('speechwarp_write');
final _writeInt16 = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Int16>, Int32),
    int Function(Pointer<Void>, Pointer<Int16>, int)>('speechwarp_write_i16');
final _read = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Float>, Int32),
    int Function(Pointer<Void>, Pointer<Float>, int)>('speechwarp_read');
final _readInt16 = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Int16>, Int32),
    int Function(Pointer<Void>, Pointer<Int16>, int)>('speechwarp_read_i16');
final _available =
    _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>('speechwarp_available');
final _flush = _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>('speechwarp_flush');
final _reset = _library.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('speechwarp_reset');
final _position =
    _library.lookupFunction<Int64 Function(Pointer<Void>), int Function(Pointer<Void>)>('speechwarp_position');

final _setHeardPause = _library.lookupFunction<Void Function(Pointer<Void>, Float, Float),
    void Function(Pointer<Void>, double, double)>('speechwarp_set_heard_pause');
final _getHeardPause = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_heard_pause');
final _getHeardPauseFrom = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_heard_pause_from');
final _setFloorBlend = _library.lookupFunction<Void Function(Pointer<Void>, Float, Float, Float),
    void Function(Pointer<Void>, double, double, double)>('speechwarp_set_floor_blend');
final _getFloorBlend = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_floor_blend');
final _getFloorBlendFrom = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_floor_blend_from');
final _getFloorBlendFull = _library
    .lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_floor_blend_full');

// Frees the native stream of a SpeechwarpStream that is dropped without close() being called.
final _finalizer = NativeFinalizer(_destroyPointer.cast());

/// Speeds up speech. Write audio in, read the faster audio out.
///
/// A stream belongs to the isolate that created it. [close] it when done.
class SpeechwarpStream implements Finalizable {
  /// The slowest speed that can be set.
  static const double minSpeed = 0.05;

  /// The fastest speed that can be set.
  static const double maxSpeed = 20;

  /// The version of the native library, such as "0.3.0".
  static String get libraryVersion => _version().toDartString();

  /// Samples per second.
  final int sampleRate;

  /// Samples in a frame.
  final int channels;

  Pointer<Void> _stream;

  // Native memory through which audio is passed, grown as needed and kept between calls.
  Pointer<Uint8> _scratch = nullptr;
  int _scratchBytes = 0;

  /// Creates a stream at speed 1 with nonlinear speed-up on.
  ///
  /// [sampleRate] is 4000 to 384000 and [channels] 1 to 32.
  SpeechwarpStream(this.sampleRate, {this.channels = 1}) : _stream = nullptr {
    RangeError.checkValueInInterval(sampleRate, 4000, 384000, 'sampleRate');
    RangeError.checkValueInInterval(channels, 1, 32, 'channels');
    _stream = _create(sampleRate, channels);
    if (_stream == nullptr) throw const OutOfMemoryError();
    _finalizer.attach(this, _stream, detach: this);
  }

  /// Overall speed: 2 plays twice as fast. Clamped to [minSpeed]..[maxSpeed].
  ///
  /// Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
  /// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
  /// result within a few percent.
  double get speed => _getSpeed(_open());
  set speed(double value) {
    if (!(value > 0)) throw ArgumentError.value(value, 'speed', 'must be greater than zero');
    _setSpeed(_open(), value);
  }

  /// How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
  /// pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
  double get nonlinear => _getNonlinear(_open());
  set nonlinear(double value) {
    if (value.isNaN) throw ArgumentError.value(value, 'nonlinear', 'must be a number');
    _setNonlinear(_open(), value);
  }

  // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
  // docs/how-it-works.md.

  /// Shortens every pause to at most this many seconds of input before speeding up, so that the speed is spent
  /// on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. [position] counts the
  /// frames left out.
  double get pauseCap => _getPauseCap(_open());
  set pauseCap(double value) => _setPauseCap(_open(), _number(value, 'pauseCap'));

  /// While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses is
  /// spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
  /// When false, trimmed pauses make playback faster than [speed].
  bool get keepSpeed => _getKeepSpeed(_open()) != 0;
  set keepSpeed(bool value) => _setKeepSpeed(_open(), value ? 1 : 0);

  /// No 10 ms block of speech plays slower than this fraction of [speed]: at 8x, 0.5 keeps every block at 4x or
  /// more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with nonlinear
  /// speed-up above 1x.
  double get speedFloor => _getSpeedFloor(_open());
  set speedFloor(double value) => _setSpeedFloor(_open(), _number(value, 'speedFloor'));

  /// Seconds of silence put into the output [rhythmRate] times a second, at the quietest point nearby, which can
  /// help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06. Allowed: 0,
  /// or 0.005 to 0.2. [position] holds still during a gap.
  double get rhythmGap => _getRhythmGap(_open());
  set rhythmGap(double value) => _setRhythmGap(_open(), _number(value, 'rhythmGap'));

  /// Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8.
  double get rhythmRate => _getRhythmRate(_open());
  set rhythmRate(double value) {
    if (!(value > 0)) throw ArgumentError.value(value, 'rhythmRate', 'must be greater than zero');
    _setRhythmRate(_open(), value);
  }

  // Options that follow the speed. See docs/how-it-works.md.

  /// Keeps each pause about [seconds] long in the output: the pause cap in force is [seconds] times the current
  /// speed, clamped to 0.03 to 0.4 s of input, and is applied whenever [speed] changes. Below [fromSpeed] pauses
  /// are left alone. [seconds]: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to 0.4.
  /// [fromSpeed]: 1 to 20. Sensible: 0.015 to 0.06 from 3x. Setting [pauseCap] turns the rule off.
  void setHeardPause(double seconds, double fromSpeed) =>
      _setHeardPause(_open(), _number(seconds, 'seconds'), _number(fromSpeed, 'fromSpeed'));

  /// The `seconds` last given to [setHeardPause]; 0 while the rule is off.
  double get heardPause => _getHeardPause(_open());

  /// The `fromSpeed` last given to [setHeardPause].
  double get heardPauseFrom => _getHeardPauseFrom(_open());

  /// The speed floor in force is 0 below [fromSpeed], rises linearly to [fraction] at [fullSpeed] and stays there
  /// above it, so that a speed ramp never changes the sound in a jump. [fraction]: 0 turns the rule off (and the
  /// floor with it); otherwise up to 1. Speeds 1 to 20; if [fullSpeed] is not above [fromSpeed] the floor steps
  /// to [fraction] at [fromSpeed]. Sensible: 0.5 from 4x, full at 6x. Setting [speedFloor] turns the rule off.
  void setFloorBlend(double fraction, double fromSpeed, double fullSpeed) => _setFloorBlend(
      _open(), _number(fraction, 'fraction'), _number(fromSpeed, 'fromSpeed'), _number(fullSpeed, 'fullSpeed'));

  /// The `fraction` last given to [setFloorBlend]; 0 while the rule is off.
  double get floorBlend => _getFloorBlend(_open());

  /// The `fromSpeed` last given to [setFloorBlend].
  double get floorBlendFrom => _getFloorBlendFrom(_open());

  /// The `fullSpeed` last given to [setFloorBlend].
  double get floorBlendFull => _getFloorBlendFull(_open());

  /// Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
  /// been written since creation or [reset]. Multiply by the speed for the rate heard. An estimate, typically
  /// within about 10%.
  double? get syllableRate {
    final rate = _syllableRate(_open());
    return rate < 0 ? null : rate;
  }

  static double _number(double value, String name) {
    if (value.isNaN) throw ArgumentError.value(value, name, 'must be a number');
    return value;
  }

  /// Frames of output ready to read.
  int get framesAvailable => _available(_open());

  /// The input frame, counted from creation or the last [reset], that the next output frame to be read was
  /// made from. This is how a player maps what is being heard back to a place in the source.
  ///
  /// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
  /// [flush] has been read it equals the number of frames written.
  int get position => _position(_open());

  /// Adds interleaved input. The output does not depend on how the input is divided between calls.
  void write(Float32List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final native = _room(samples.length * 4).cast<Float>();
    native.asTypedList(samples.length).setAll(0, samples);
    if (_write(_stream, native, frames) == 0) throw const OutOfMemoryError();
  }

  /// Adds interleaved 16-bit input.
  void writeInt16(Int16List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final native = _room(samples.length * 2).cast<Int16>();
    native.asTypedList(samples.length).setAll(0, samples);
    if (_writeInt16(_stream, native, frames) == 0) throw const OutOfMemoryError();
  }

  /// Takes processed output: everything that is ready, or at most [maxFrames] frames of it.
  ///
  /// The list may be empty: output lags input by a short look-ahead.
  Float32List read([int? maxFrames]) {
    final frames = _framesToRead(maxFrames);
    if (frames == 0) return Float32List(0);
    final native = _room(frames * channels * 4).cast<Float>();
    final got = _read(_stream, native, frames);
    return Float32List.fromList(native.asTypedList(got * channels));
  }

  /// Takes processed output as 16-bit samples. See [read].
  Int16List readInt16([int? maxFrames]) {
    final frames = _framesToRead(maxFrames);
    if (frames == 0) return Int16List(0);
    final native = _room(frames * channels * 2).cast<Int16>();
    final got = _readInt16(_stream, native, frames);
    return Int16List.fromList(native.asTypedList(got * channels));
  }

  /// Processes everything written so far, at the end of the input. Read afterwards to get the rest. Writing
  /// more starts a new stretch of audio, and [position] carries on counting.
  void flush() {
    if (_flush(_open()) == 0) throw const OutOfMemoryError();
  }

  /// Discards all buffered input and output, keeping the speed and nonlinear settings, and starts [position]
  /// again from zero. Use after seeking.
  void reset() => _reset(_open());

  /// Frees the native stream. Using the stream afterwards throws [StateError].
  void close() {
    if (_stream == nullptr) return;
    _finalizer.detach(this);
    _destroy(_stream);
    _stream = nullptr;
    calloc.free(_scratch);
    _scratch = nullptr;
    _scratchBytes = 0;
  }

  Pointer<Void> _open() {
    if (_stream == nullptr) throw StateError('the stream is closed');
    return _stream;
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

  Pointer<Uint8> _room(int bytes) {
    if (bytes > _scratchBytes) {
      calloc.free(_scratch);
      _scratchBytes = bytes > 2 * _scratchBytes ? bytes : 2 * _scratchBytes;
      _scratch = calloc<Uint8>(_scratchBytes);
    }
    return _scratch;
  }
}

final _syllablesCreate = _library.lookupFunction<Pointer<Void> Function(Int32, Int32), Pointer<Void> Function(int, int)>(
    'speechwarp_syllables_create');
final _syllablesDestroyPointer = _library.lookup<NativeFunction<Void Function(Pointer<Void>)>>('speechwarp_syllables_destroy');
final _syllablesDestroy = _syllablesDestroyPointer.asFunction<void Function(Pointer<Void>)>();
final _syllablesWrite = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Float>, Int32),
    int Function(Pointer<Void>, Pointer<Float>, int)>('speechwarp_syllables_write');
final _syllablesWriteInt16 = _library.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Int16>, Int32),
    int Function(Pointer<Void>, Pointer<Int16>, int)>('speechwarp_syllables_write_i16');
final _syllablesRate = _library.lookupFunction<Double Function(Pointer<Void>, Double, Double), double Function(Pointer<Void>, double, double)>(
    'speechwarp_syllables_rate');
final _syllablesReset = _library.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'speechwarp_syllables_reset');
final _trainerCreate = _library.lookupFunction<Pointer<Void> Function(Uint64), Pointer<Void> Function(int)>(
    'speechwarp_trainer_create');
final _trainerDestroyPointer = _library.lookup<NativeFunction<Void Function(Pointer<Void>)>>('speechwarp_trainer_destroy');
final _trainerDestroy = _trainerDestroyPointer.asFunction<void Function(Pointer<Void>)>();
final _trainerSetWeight = _library.lookupFunction<Void Function(Pointer<Void>, Int32, Double), void Function(Pointer<Void>, int, double)>(
    'speechwarp_trainer_set_weight');
final _trainerGetWeight = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_get_weight');
final _trainerSetParam = _library.lookupFunction<Void Function(Pointer<Void>, Int32, Double), void Function(Pointer<Void>, int, double)>(
    'speechwarp_trainer_set_param');
final _trainerGetParam = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_get_param');
final _trainerAddMeasure = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Double, Double, Double), int Function(Pointer<Void>, int, double, double, double, double)>(
    'speechwarp_trainer_add_measure');
final _trainerTestBegin = _library.lookupFunction<Void Function(Pointer<Void>, Double, Double), void Function(Pointer<Void>, double, double)>(
    'speechwarp_trainer_test_begin');
final _trainerTestRate = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_test_rate');
final _trainerTestDone = _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
    'speechwarp_trainer_test_done');
final _trainerTestEnd = _library.lookupFunction<Double Function(Pointer<Void>, Double), double Function(Pointer<Void>, double)>(
    'speechwarp_trainer_test_end');
final _trainerThreshold = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_threshold');
final _trainerThresholdLow = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_threshold_low');
final _trainerThresholdHigh = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_threshold_high');
final _trainerSessionBegin = _library.lookupFunction<Void Function(Pointer<Void>, Int32, Double), void Function(Pointer<Void>, int, double)>(
    'speechwarp_trainer_session_begin');
final _trainerSessionRate = _library.lookupFunction<Double Function(Pointer<Void>, Double), double Function(Pointer<Void>, double)>(
    'speechwarp_trainer_session_rate');
final _trainerSessionEnd = _library.lookupFunction<Int32 Function(Pointer<Void>, Double, Double), int Function(Pointer<Void>, double, double)>(
    'speechwarp_trainer_session_end');
final _trainerAddRetention = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Double, Double, Double), int Function(Pointer<Void>, int, double, double, double, double)>(
    'speechwarp_trainer_add_retention');
final _trainerNextPlan = _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
    'speechwarp_trainer_next_plan');
final _trainerPlanEffect = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_effect');
final _trainerPlanEffectSd = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_effect_sd');
final _trainerPlanRetention = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_retention');
final _trainerPlanRetentionSd = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_retention_sd');
final _trainerPlanSessions = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32), int Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_sessions');
final _trainerPlanBestProbability = _library.lookupFunction<Double Function(Pointer<Void>, Int32), double Function(Pointer<Void>, int)>(
    'speechwarp_trainer_plan_best_probability');
final _trainerTrend = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_trend');
final _trainerTrendSd = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trainer_trend_sd');
final _trialsCreate = _library.lookupFunction<Pointer<Void> Function(Uint64), Pointer<Void> Function(int)>(
    'speechwarp_trials_create');
final _trialsDestroyPointer = _library.lookup<NativeFunction<Void Function(Pointer<Void>)>>('speechwarp_trials_destroy');
final _trialsDestroy = _trialsDestroyPointer.asFunction<void Function(Pointer<Void>)>();
final _trialsAddSetting = _library.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
    'speechwarp_trials_add_setting');
final _trialsAddValue = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double), int Function(Pointer<Void>, int, double)>(
    'speechwarp_trials_add_value');
final _trialsSetAvailable = _library.lookupFunction<Void Function(Pointer<Void>, Int32, Int32), void Function(Pointer<Void>, int, int)>(
    'speechwarp_trials_set_available');
final _trialsAdd = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Double, Double, Double, Double, Int32), int Function(Pointer<Void>, int, double, double, double, double, double, int)>(
    'speechwarp_trials_add');
final _trialsNext = _library.lookupFunction<Int32 Function(Pointer<Void>, Double), int Function(Pointer<Void>, double)>(
    'speechwarp_trials_next');
final _trialsNextFirst = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trials_next_first');
final _trialsNextSecond = _library.lookupFunction<Double Function(Pointer<Void>), double Function(Pointer<Void>)>(
    'speechwarp_trials_next_second');
final _trialsWon = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Int32), int Function(Pointer<Void>, int, double, int)>(
    'speechwarp_trials_won');
final _trialsLost = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Int32), int Function(Pointer<Void>, int, double, int)>(
    'speechwarp_trials_lost');
final _trialsTied = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Int32), int Function(Pointer<Void>, int, double, int)>(
    'speechwarp_trials_tied');
final _trialsHeard = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double, Int32), int Function(Pointer<Void>, int, double, int)>(
    'speechwarp_trials_heard');
final _trialsMeanScore = _library.lookupFunction<Double Function(Pointer<Void>, Int32, Double, Int32), double Function(Pointer<Void>, int, double, int)>(
    'speechwarp_trials_mean_score');
final _trialsWinner = _library.lookupFunction<Int32 Function(Pointer<Void>, Int32, Double), int Function(Pointer<Void>, int, double)>(
    'speechwarp_trials_winner');
final _trialsSetConfidence = _library.lookupFunction<Void Function(Pointer<Void>, Double), void Function(Pointer<Void>, double)>(
    'speechwarp_trials_set_confidence');

final _syllablesFinalizer = NativeFinalizer(_syllablesDestroyPointer.cast());
final _trainerFinalizer = NativeFinalizer(_trainerDestroyPointer.cast());
final _trialsFinalizer = NativeFinalizer(_trialsDestroyPointer.cast());

double _checked(double value, String name) {
  if (value.isNaN) throw ArgumentError.value(value, name, 'must be a number');
  return value;
}

/// The syllable counter of a stream, on its own: for audio that does not go through a stream (a player using
/// some other speed-up, or measuring a file). A counter given the same input as a stream reports the same rate as
/// [SpeechwarpStream.syllableRate]. [close] it when done.
class SyllableCounter implements Finalizable {
  /// Samples per second.
  final int sampleRate;

  /// Samples in a frame.
  final int channels;

  Pointer<Void> _counter;
  Pointer<Uint8> _scratch = nullptr;
  int _scratchBytes = 0;

  /// Creates a counter. [sampleRate] is 4000 to 384000 and [channels] 1 to 32.
  SyllableCounter(this.sampleRate, {this.channels = 1}) : _counter = nullptr {
    RangeError.checkValueInInterval(sampleRate, 4000, 384000, 'sampleRate');
    RangeError.checkValueInInterval(channels, 1, 32, 'channels');
    _counter = _syllablesCreate(sampleRate, channels);
    if (_counter == nullptr) throw const OutOfMemoryError();
    _syllablesFinalizer.attach(this, _counter, detach: this);
  }

  /// Adds interleaved samples in the range -1 to 1.
  void write(Float32List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final native = _room(samples.length * 4).cast<Float>();
    native.asTypedList(samples.length).setAll(0, samples);
    if (_syllablesWrite(_counter, native, frames) == 0) throw StateError('the samples could not be counted');
  }

  /// Adds interleaved 16-bit samples.
  void writeInt16(Int16List samples) {
    final frames = _wholeFrames(samples.length);
    if (frames == 0) return;
    final native = _room(samples.length * 2).cast<Int16>();
    native.asTypedList(samples.length).setAll(0, samples);
    if (_syllablesWriteInt16(_counter, native, frames) == 0) throw StateError('the samples could not be counted');
  }

  /// Syllables a second over the last [windowSeconds] written (or all of it, if less); null until
  /// [minimumSeconds] have been written. The window is clamped to 1 to 120 s, the minimum to 0 to the window.
  double? rate({double windowSeconds = 60, double minimumSeconds = 10}) {
    final value =
        _syllablesRate(_open(), _checked(windowSeconds, 'windowSeconds'), _checked(minimumSeconds, 'minimumSeconds'));
    return value < 0 ? null : value;
  }

  /// Forgets everything written.
  void reset() => _syllablesReset(_open());

  /// Frees the native counter. Using it afterwards throws [StateError].
  void close() {
    if (_counter == nullptr) return;
    _syllablesFinalizer.detach(this);
    _syllablesDestroy(_counter);
    _counter = nullptr;
    calloc.free(_scratch);
    _scratch = nullptr;
    _scratchBytes = 0;
  }

  Pointer<Void> _open() {
    if (_counter == nullptr) throw StateError('the syllable counter is closed');
    return _counter;
  }

  int _wholeFrames(int samples) {
    _open();
    if (samples % channels != 0) {
      throw ArgumentError('$samples samples is not a whole number of $channels-channel frames');
    }
    return samples ~/ channels;
  }

  Pointer<Uint8> _room(int bytes) {
    if (bytes > _scratchBytes) {
      calloc.free(_scratch);
      _scratchBytes = bytes > 2 * _scratchBytes ? bytes : 2 * _scratchBytes;
      _scratch = calloc<Uint8>(_scratchBytes);
    }
    return _scratch;
  }
}

/// What a score in 0..1 measures.
enum TrainerMeasure {
  /// Share of the words said back correctly from a sentence heard once.
  intelligibility(0),

  /// Share right on "was this sentence in what you just heard?" items. Chance is 0.5.
  verification(1),

  /// Verification items about a session's material, answered after a delay; see [ListenerTrainer.addRetention].
  retention(2),

  /// The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4.
  rating(3);

  const TrainerMeasure(this.value);

  /// The number the C library uses.
  final int value;
}

/// Session plans: how the rate moves during a session.
enum TrainerPlan {
  /// The threshold plus a margin, all session.
  steady(0),

  /// Start below the threshold and step up to threshold plus margin.
  ramp(1),

  /// Alternate periods above and below the threshold.
  interval(2),

  /// Move up or down after each in-session check, to stay at the target.
  tracking(3);

  const TrainerPlan(this.value);

  /// The number the C library uses.
  final int value;
}

/// Tunable numbers of the trainer, with their defaults.
enum TrainerParam {
  /// Share understood that defines the threshold: 0.75 (0.5 to 0.95).
  target(0),

  /// Steady and ramp: aim this fraction above the threshold: 0.10.
  margin(1),

  /// Ramp: start at this fraction of the target rate: 0.8.
  rampStart(2),

  /// Ramp: step by this fraction of the target rate: 0.02.
  rampStep(3),

  /// Ramp: minutes between steps: 2.
  rampMinutes(4),

  /// Interval: this fraction above, then below, the threshold: 0.15.
  intervalSpread(5),

  /// Interval: minutes in each period: 10.
  intervalMinutes(6),

  /// Tracking: ln(rate) moves by gain x (score - target) per check: 0.4.
  trackingGain(7),

  /// Plans: threshold gain an hour worth a whole unit of retention: 0.2.
  retentionCost(8),

  /// Threshold test: most presentations: 40.
  testMax(9),

  /// Threshold test: done when the 95% interval's high / low is below this: 1.25.
  testPrecision(10);

  const TrainerParam(this.value);

  /// The number the C library uses.
  final int value;
}

/// Training a listener to follow faster speech: pure logic with no audio, clock or storage. Scores, rates and
/// timestamps go in as plain numbers; rates and plans come out. Deterministic: two trainers with the same seed
/// given the same calls give the same answers, so keep a log of calls and replay it to restore state.
///
/// The unit of rate everywhere is syllables a second heard: the source's syllable rate times the speed. Times are
/// seconds on any clock, and only differences are used. NaN arguments are ignored. [close] it when done.
class ListenerTrainer implements Finalizable {
  Pointer<Void> _trainer;

  /// Creates a trainer. [seed] seeds its random choices; it is an unsigned 64-bit number, so a negative [int]
  /// stands for its two's complement.
  ListenerTrainer({int seed = 0}) : _trainer = nullptr {
    _trainer = _trainerCreate(seed);
    if (_trainer == nullptr) throw const OutOfMemoryError();
    _trainerFinalizer.attach(this, _trainer, detach: this);
  }

  /// How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification 1,
  /// retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
  void setWeight(TrainerMeasure kind, double weight) => _trainerSetWeight(_open(), kind.value, weight);

  /// The weight of a kind of measure; see [setWeight].
  double getWeight(TrainerMeasure kind) => _trainerGetWeight(_open(), kind.value);

  /// Sets a tunable number; see [TrainerParam] for the defaults.
  void setParam(TrainerParam param, double value) => _trainerSetParam(_open(), param.value, value);

  /// A tunable number.
  double getParam(TrainerParam param) => _trainerGetParam(_open(), param.value);

  /// Records a score: [kind] (not retention), [score] 0..1, from [items] items (for a sentence repeated back, the
  /// number of words scored; for verification, the number of questions; for a rating, 1), heard at [rate]
  /// syllables a second, at [time]. During a threshold test it updates the estimate; during a session it is an
  /// in-session check, and the tracking plan reacts to it. Returns false if an argument is invalid.
  bool addMeasure(TrainerMeasure kind, double score, double items, double rate, double time) =>
      _trainerAddMeasure(_open(), kind.value, score, items, rate, time) != 0;

  /// Starts a threshold test: estimates the rate understood [TrainerParam.target] (75%) of the time, by the psi
  /// method. The prior is log-normal around [priorRate] or, if that is 0, around the last estimate, or failing
  /// that around 10 syllables a second, within 3 to 60.
  void testBegin(double priorRate, double time) => _trainerTestBegin(_open(), priorRate, time);

  /// The rate to present next.
  double testRate() => _trainerTestRate(_open());

  /// True once the 95% interval is narrower than [TrainerParam.testPrecision] (at least 8 presentations) or
  /// [TrainerParam.testMax] presentations have been scored; false otherwise or if no test is running.
  bool get testDone => _trainerTestDone(_open()) != 0;

  /// Finishes the test; the estimate becomes the current threshold. Returns it (0 if no test was running).
  double testEnd(double time) => _trainerTestEnd(_open(), time);

  /// The current estimate (posterior median): of the running test, else of the last one finished; 0 if none.
  double get threshold => _trainerThreshold(_open());

  /// The low end of the 95% interval of the estimate.
  double get thresholdLow => _trainerThresholdLow(_open());

  /// The high end of the 95% interval of the estimate.
  double get thresholdHigh => _trainerThresholdHigh(_open());

  /// Begins a session under [plan].
  void sessionBegin(TrainerPlan plan, double time) => _trainerSessionBegin(_open(), plan.value, time);

  /// The rate to play at now, under the session's plan, from the threshold at [sessionBegin]. 0 if no session.
  double sessionRate(double time) => _trainerSessionRate(_open(), time);

  /// Ends the session after [listeningHours] of listening in it. It is recorded for comparing plans if a
  /// threshold test ended after it began. Returns the session's number (0, 1, ...) for [addRetention], or -1.
  int sessionEnd(double listeningHours, double time) => _trainerSessionEnd(_open(), listeningHours, time);

  /// Retention for a recorded [session]: [score] 0..1 from [items] items, answered [delaySeconds] after it ended.
  /// Returns false if an argument is invalid.
  bool addRetention(int session, double score, double items, double delaySeconds, double time) =>
      _trainerAddRetention(_open(), session, score, items, delaySeconds, time) != 0;

  /// The plan to run next: a Thompson draw (advances the random source).
  TrainerPlan nextPlan() => TrainerPlan.values[_trainerNextPlan(_open())];

  /// A plan's estimated threshold gain an hour now, as a fraction: 0.01 is 1% an hour.
  double planEffect(TrainerPlan plan) => _trainerPlanEffect(_open(), plan.value);

  /// The standard deviation of [planEffect].
  double planEffectSd(TrainerPlan plan) => _trainerPlanEffectSd(_open(), plan.value);

  /// A plan's retention; NaN without data.
  double planRetention(TrainerPlan plan) => _trainerPlanRetention(_open(), plan.value);

  /// The standard deviation of [planRetention]; NaN without data.
  double planRetentionSd(TrainerPlan plan) => _trainerPlanRetentionSd(_open(), plan.value);

  /// Sessions recorded under a plan.
  int planSessions(TrainerPlan plan) => _trainerPlanSessions(_open(), plan.value);

  /// The probability that a plan is the best by utility (does not advance the random source).
  double planBestProbability(TrainerPlan plan) => _trainerPlanBestProbability(_open(), plan.value);

  /// H, the hours of listening by which gains have halved (1000 standing for "not slowing").
  double get trend => _trainerTrend(_open());

  /// The uncertainty of [trend] as a standard deviation of ln H.
  double get trendSd => _trainerTrendSd(_open());

  /// Frees the native trainer. Using it afterwards throws [StateError].
  void close() {
    if (_trainer == nullptr) return;
    _trainerFinalizer.detach(this);
    _trainerDestroy(_trainer);
    _trainer = nullptr;
  }

  Pointer<Void> _open() {
    if (_trainer == nullptr) throw StateError('the trainer is closed');
    return _trainer;
  }
}

/// Designing the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
/// values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
/// caller names the settings; here they are numbers. Deterministic for a given seed. [close] it when done.
class BlindTrials implements Finalizable {
  Pointer<Void> _trials;

  /// Creates a designer. [seed] seeds its random choices; it is an unsigned 64-bit number, so a negative [int]
  /// stands for its two's complement.
  BlindTrials({int seed = 0}) : _trials = nullptr {
    _trials = _trialsCreate(seed);
    if (_trials == nullptr) throw const OutOfMemoryError();
    _trialsFinalizer.attach(this, _trials, detach: this);
  }

  /// Adds a setting; returns its number (0, 1, ...), or -1.
  int addSetting() => _trialsAddSetting(_open());

  /// Adds a value to compare; returns its number within the setting, or -1 (duplicate, bad setting).
  int addValue(int setting, double value) => _trialsAddValue(_open(), setting, value);

  /// Leaves a setting out of [next] while false (say, when its method is not available). Default true.
  void setAvailable(int setting, bool available) => _trialsSetAvailable(_open(), setting, available ? 1 : 0);

  /// Records a trial at [speed]: the two values in the order heard, each one's score 0..1, and [preferred]: -1 the
  /// first, 1 the second, 0 neither. Values must be ones added. Returns false if an argument is invalid.
  bool add(int setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore,
          int preferred) =>
      _trialsAdd(_open(), setting, speed, firstValue, secondValue, firstScore, secondScore, preferred) != 0;

  /// Chooses the next trial at [speed]: the available setting with the fewest trials in its band (ties at random),
  /// its pair of values compared least (ties at random), in random order. Null if no setting has two values.
  ({int setting, double first, double second})? next(double speed) {
    final trials = _open();
    final setting = _trialsNext(trials, speed);
    if (setting < 0) return null;
    return (setting: setting, first: _trialsNextFirst(trials), second: _trialsNextSecond(trials));
  }

  /// Comparisons won by one value of a setting in the band of [speed].
  int won(int setting, double speed, int value) => _trialsWon(_open(), setting, speed, value);

  /// Comparisons lost by one value of a setting in the band of [speed].
  int lost(int setting, double speed, int value) => _trialsLost(_open(), setting, speed, value);

  /// Comparisons tied by one value of a setting in the band of [speed].
  int tied(int setting, double speed, int value) => _trialsTied(_open(), setting, speed, value);

  /// Trials one value of a setting was heard in, in the band of [speed].
  int heard(int setting, double speed, int value) => _trialsHeard(_open(), setting, speed, value);

  /// The mean score of one value in the band of [speed]; null if never heard.
  double? meanScore(int setting, double speed, int value) {
    final score = _trialsMeanScore(_open(), setting, speed, value);
    return score.isNaN ? null : score;
  }

  /// The value with a reliable win in that band, or null. A value wins when it has been heard in at least 5
  /// trials, has met every other value in at least 3, and against each the Bayes factor for "preferred" over "no
  /// preference" is at least 1 / (1 - confidence): 20 at the default 0.95.
  int? winner(int setting, double speed) {
    final value = _trialsWinner(_open(), setting, speed);
    return value < 0 ? null : value;
  }

  /// The confidence [winner] demands, 0 to 1; default 0.95.
  void setConfidence(double confidence) => _trialsSetConfidence(_open(), confidence);

  /// Frees the native trials. Using them afterwards throws [StateError].
  void close() {
    if (_trials == nullptr) return;
    _trialsFinalizer.detach(this);
    _trialsDestroy(_trials);
    _trials = nullptr;
  }

  Pointer<Void> _open() {
    if (_trials == nullptr) throw StateError('the trials are closed');
    return _trials;
  }
}
