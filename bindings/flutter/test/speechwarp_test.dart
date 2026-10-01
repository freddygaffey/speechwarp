// Runs on this computer's Dart VM, so it needs the library built for this computer:
//
//     cmake -B build && cmake --build build          (at the top of the repository)
//     SPEECHWARP_LIBRARY=$PWD/build/libspeechwarp.dylib flutter test     (.so on Linux)
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:speechwarp/speechwarp.dart';

const rate = 22050;

/// Something for the library to chew on: a gliding tone in bursts, with gaps.
Float32List signal(double seconds, {int channels = 1}) {
  final frames = (rate * seconds).toInt();
  final samples = Float32List(frames * channels);
  var phase = 0.0;
  for (var i = 0; i < frames; i++) {
    final t = i / rate;
    phase += 2 * pi * (120 + 60 * sin(t * 3)) / rate;
    final envelope = t % 0.4 < 0.3 ? sin(pi * (t % 0.4) / 0.3) : 0.0;
    final value = 0.4 * envelope * (sin(phase) + 0.5 * sin(2 * phase) + 0.3 * sin(3 * phase));
    for (var c = 0; c < channels; c++) {
      samples[i * channels + c] = value;
    }
  }
  return samples;
}

Float32List speedUp(Float32List input, double speed, {double nonlinear = 1}) {
  final stream = SpeechwarpStream(rate)
    ..speed = speed
    ..nonlinear = nonlinear
    ..write(input)
    ..flush();
  final out = stream.read();
  stream.close();
  return out;
}

void main() {
  test('the version matches the header and the package', () {
    final header = File('../../include/speechwarp.h').readAsStringSync();
    final pubspec = File('pubspec.yaml').readAsStringSync();
    final version = RegExp(r'#define SPEECHWARP_VERSION "(.*)"').firstMatch(header)!.group(1);
    expect(SpeechwarpStream.libraryVersion, version);
    expect(RegExp(r'^version: (\S+)', multiLine: true).firstMatch(pubspec)!.group(1), version);
  });

  test('defaults and arguments', () {
    final stream = SpeechwarpStream(rate, channels: 2);
    expect([stream.sampleRate, stream.channels, stream.speed, stream.nonlinear], [rate, 2, 1, 1]);
    expect([stream.framesAvailable, stream.position, stream.read().length], [0, 0, 0]);

    expect(() => SpeechwarpStream(100), throwsRangeError);
    expect(() => SpeechwarpStream(rate, channels: 0), throwsRangeError);
    expect(() => stream.speed = 0, throwsArgumentError);
    expect(() => stream.speed = double.nan, throwsArgumentError);
    expect(() => stream.nonlinear = double.nan, throwsArgumentError);
    expect(() => stream.write(Float32List(3)), throwsArgumentError);

    stream.speed = 1000;
    expect(stream.speed, SpeechwarpStream.maxSpeed);
    stream.nonlinear = -3;
    expect(stream.nonlinear, 0);

    stream.close();
    stream.close();
    expect(() => stream.framesAvailable, throwsStateError);
    expect(() => stream.write(Float32List(4)), throwsStateError);
  });

  test('output is shorter by the speed', () {
    final input = signal(20);
    for (final (speed, nonlinear) in [(1.0, 1.0), (3.0, 1.0), (3.0, 0.0), (8.0, 1.0)]) {
      final output = speedUp(input, speed, nonlinear: nonlinear);
      expect(input.length / output.length / speed, closeTo(1, 0.1), reason: 'speed $speed');
    }
  });

  test('stereo and 16-bit', () {
    final floats = signal(5, channels: 2);
    final input = Int16List.fromList([for (final x in floats) (x * 32767).toInt()]);
    final stream = SpeechwarpStream(rate, channels: 2)
      ..speed = 2
      ..writeInt16(input)
      ..flush();
    var total = 0;
    while (true) {
      final piece = stream.readInt16(1000);
      if (piece.isEmpty) break;
      expect(piece.length, lessThanOrEqualTo(2000));
      for (var i = 0; i < piece.length; i += 2) {
        expect(piece[i], piece[i + 1]);
      }
      total += piece.length ~/ 2;
    }
    expect(input.length / 2 / total, closeTo(2, 0.2));
    stream.close();
  });

  test('streaming matches one shot, and position follows', () {
    final input = signal(20);
    final stream = SpeechwarpStream(rate)..speed = 4;
    final pieces = <double>[];
    var last = 0;
    for (var start = 0; start < input.length; start += 1000) {
      final end = min(start + 1000, input.length);
      stream.write(Float32List.sublistView(input, start, end));
      while (stream.framesAvailable >= 256) {
        expect(stream.position, inInclusiveRange(last, end));
        last = stream.position;
        pieces.addAll(stream.read(256));
      }
    }
    stream.flush();
    pieces.addAll(stream.read());
    expect(stream.position, input.length);
    expect(pieces, speedUp(input, 4));
    stream.close();
  });

  test('reset discards everything', () {
    final input = signal(5);
    final stream = SpeechwarpStream(rate)
      ..speed = 3
      ..write(input);
    expect(stream.framesAvailable, greaterThan(0));
    stream.reset();
    expect([stream.framesAvailable, stream.position, stream.speed], [0, 0, 3]);
    stream
      ..write(input)
      ..flush();
    expect(stream.read(), speedUp(input, 3));
    stream.close();
  });
}
