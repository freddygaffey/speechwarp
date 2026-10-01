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
final _setNonlinear = _library.lookupFunction<Void Function(Pointer<Void>, Float),
    void Function(Pointer<Void>, double)>('speechwarp_set_nonlinear');
final _getNonlinear =
    _library.lookupFunction<Float Function(Pointer<Void>), double Function(Pointer<Void>)>('speechwarp_get_nonlinear');
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

  /// The version of the native library, such as "0.1.0".
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
