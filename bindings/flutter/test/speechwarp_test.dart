// On this computer's Dart VM, this needs the library built for this computer:
//
//     cmake -B build && cmake --build build          (at the top of the repository)
//     SPEECHWARP_LIBRARY=$PWD/build/libspeechwarp.dylib flutter test     (.so on Linux)
//
// In a browser, it needs the WebAssembly that scripts/build_web.sh makes (it needs Emscripten):
//
//     sh scripts/build_web.sh && flutter test --platform chrome
//
// The same tests run on both.
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:speechwarp/speechwarp.dart';

import 'source_files.dart';

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
  setUpAll(Speechwarp.initialize);

  test('initialize is ready, and stays ready when called again', () async {
    expect(Speechwarp.isInitialized, isTrue);
    await Speechwarp.initialize();
    await Speechwarp.initialize();
  });

  test('the version matches the header and the package', () {
    final header = readSourceFile('../../include/speechwarp.h');
    final pubspec = readSourceFile('pubspec.yaml');
    if (header == null || pubspec == null) {
      // In a browser the files cannot be read; the WebAssembly was built from the header, so say what it is.
      expect(SpeechwarpStream.libraryVersion, matches(RegExp(r'^\d+\.\d+\.\d+')));
      return;
    }
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

  test('the options for high speeds start off and are clamped', () {
    final stream = SpeechwarpStream(rate);
    expect([stream.pauseCap, stream.keepSpeed, stream.speedFloor, stream.rhythmGap, stream.rhythmRate],
        [0, true, 0, 0, 5]);
    expect(stream.syllableRate, isNull);
    stream
      ..pauseCap = 5
      ..speedFloor = 0.5
      ..rhythmGap = 0.04
      ..rhythmRate = 100
      ..keepSpeed = false;
    expect([stream.pauseCap, stream.speedFloor, stream.rhythmRate, stream.keepSpeed], [1, 0.5, 16, false]);
    expect(stream.rhythmGap, closeTo(0.04, 1e-6));
    expect(() => stream.pauseCap = double.nan, throwsArgumentError);
    expect(() => stream.rhythmRate = 0, throwsArgumentError);
    stream.close();
  });

  test('the pause cap shortens pauses and position still reaches the end', () {
    final input = signal(12);
    final stream = SpeechwarpStream(rate)
      ..nonlinear = 0
      ..pauseCap = 0.03
      ..keepSpeed = false
      ..write(input)
      ..flush();
    expect(stream.read().length, lessThan(input.length * 0.9));
    expect(stream.position, input.length);
    expect(stream.syllableRate, isNotNull);
    stream.close();
  });

  test('heard pause and floor blend are read back, and drive the pause cap and floor', () {
    final stream = SpeechwarpStream(rate);
    expect([stream.heardPause, stream.floorBlend], [0, 0]);
    stream.setHeardPause(0.03, 3);
    expect(stream.heardPause, closeTo(0.03, 1e-6));
    expect(stream.heardPauseFrom, 3);
    stream.speed = 5;
    expect(stream.pauseCap, closeTo(0.15, 1e-6));
    stream.speed = 2; // below the from speed: pauses are left alone
    expect(stream.pauseCap, 0);

    stream.setFloorBlend(0.5, 4, 6);
    expect([stream.floorBlend, stream.floorBlendFrom, stream.floorBlendFull], [0.5, 4, 6]);
    stream.speed = 5;
    expect(stream.speedFloor, closeTo(0.25, 1e-6));
    stream.speed = 8;
    expect(stream.speedFloor, closeTo(0.5, 1e-6));
    expect(() => stream.setHeardPause(double.nan, 3), throwsArgumentError);
    stream.close();
  });

  test('a syllable counter agrees with a stream', () {
    final input = signal(15);
    final counter = SyllableCounter(rate);
    final stream = SpeechwarpStream(rate);
    final half = rate * 5;
    counter.write(Float32List.sublistView(input, 0, half));
    expect(counter.rate(), isNull);
    counter.write(Float32List.sublistView(input, half));
    stream.write(input);
    expect(counter.rate(), greaterThan(0));
    expect(counter.rate(windowSeconds: 60, minimumSeconds: 10), stream.syllableRate);

    final ints = Int16List.fromList([for (final x in input) (x * 32767).round()]);
    final other = SyllableCounter(rate)..writeInt16(ints);
    expect(other.rate(), closeTo(counter.rate()!, 0.5));

    counter.reset();
    expect(counter.rate(), isNull);
    final stereo = SyllableCounter(rate, channels: 2);
    expect(() => stereo.write(Float32List(3)), throwsArgumentError);
    expect(() => SyllableCounter(100), throwsRangeError);
    stereo.close();
    counter.close();
    counter.close();
    expect(() => counter.rate(), throwsStateError);
    stream.close();
    other.close();
  });

  test('the listener trainer finds a threshold deterministically', () {
    final trainer = ListenerTrainer(seed: 5)..testBegin(10, 0);
    for (var i = 0; i < 12; i++) {
      final r = trainer.testRate();
      expect(trainer.addMeasure(TrainerMeasure.verification, r < 11 ? 0.95 : 0.55, 8, r, i.toDouble()), isTrue);
    }
    final threshold = trainer.testEnd(100);
    expect(threshold, greaterThan(0));
    expect(trainer.threshold, threshold);
    expect(trainer.thresholdLow, lessThan(threshold));
    expect(trainer.thresholdHigh, greaterThan(threshold));
    expect(trainer.testDone, isA<bool>());

    trainer.sessionBegin(TrainerPlan.steady, 200);
    expect(trainer.sessionRate(300), closeTo(threshold * 1.1, 1e-6));
    expect(trainer.sessionEnd(1, 3800), 0);

    expect(trainer.planEffect(TrainerPlan.ramp), 0);
    expect(trainer.planRetention(TrainerPlan.ramp), isNaN);
    expect(trainer.planSessions(TrainerPlan.ramp), 0);

    trainer.setParam(TrainerParam.target, 0.8);
    expect(trainer.getParam(TrainerParam.target), 0.8);
    trainer.setWeight(TrainerMeasure.rating, 0.1);
    expect(trainer.getWeight(TrainerMeasure.rating), closeTo(0.1, 1e-12));
    expect(trainer.addMeasure(TrainerMeasure.retention, 0.5, 1, 10, 0), isFalse);

    final a = ListenerTrainer(seed: 9);
    final b = ListenerTrainer(seed: 9);
    final plans = [for (var i = 0; i < 8; i++) a.nextPlan()];
    expect([for (var i = 0; i < 8; i++) b.nextPlan()], plans);

    for (final t in [trainer, a, b]) {
      t.close();
    }
    expect(() => trainer.threshold, throwsStateError);
  });

  test('the trainer enums carry the C values, and every parameter, retention and summary reads back', () {
    expect([for (final m in TrainerMeasure.values) m.value], [0, 1, 2, 3]);
    expect([for (final m in TrainerPlan.values) m.value], [0, 1, 2, 3]);
    expect([for (final m in TrainerParam.values) m.value], [for (var i = 0; i < 11; i++) i]);

    final trainer = ListenerTrainer(seed: 3);
    expect(trainer.getParam(TrainerParam.target), 0.75);
    expect(trainer.getParam(TrainerParam.testMax), 40);
    for (final p in TrainerParam.values) {
      expect(trainer.getParam(p).isFinite, isTrue, reason: p.name);
    }
    trainer.setParam(TrainerParam.margin, 0.2);
    expect(trainer.getParam(TrainerParam.margin), closeTo(0.2, 1e-12));

    trainer.testBegin(10, 0);
    for (var i = 0; i < 12; i++) {
      final r = trainer.testRate();
      trainer.addMeasure(TrainerMeasure.verification, r < 11 ? 0.95 : 0.55, 8, r, i.toDouble());
    }
    trainer.testEnd(100);
    trainer.sessionBegin(TrainerPlan.ramp, 200);
    trainer.testBegin(10, 300);
    for (var i = 0; i < 12; i++) {
      final r = trainer.testRate();
      trainer.addMeasure(TrainerMeasure.verification, r < 11.5 ? 0.95 : 0.55, 8, r, 300.0 + i);
    }
    trainer.testEnd(400); // a test after the session began, so the session is recorded
    final session = trainer.sessionEnd(1, 3800);
    expect(session, 0);
    expect(trainer.addRetention(session, 0.8, 8, 86400, 90000), isTrue);
    expect(trainer.addRetention(99, 0.8, 8, 86400, 90000), isFalse);
    for (final plan in TrainerPlan.values) {
      expect(trainer.planEffectSd(plan), isA<double>());
      expect(trainer.planRetentionSd(plan), isA<double>());
      expect(trainer.planBestProbability(plan), inInclusiveRange(0, 1));
    }
    expect(trainer.planSessions(TrainerPlan.ramp), 1);
    expect(trainer.trend, greaterThan(0));
    expect(trainer.trendSd, greaterThanOrEqualTo(0));
    trainer.close();
  });

  test('blind trials choose pairs and add up results', () {
    final trials = BlindTrials(seed: 1);
    expect(trials.next(5), isNull);
    final setting = trials.addSetting();
    expect(setting, 0);
    expect(trials.addValue(setting, 0), 0);
    expect(trials.addValue(setting, 0.5), 1);
    expect(trials.addValue(setting, 0.5), -1);
    final next = trials.next(5)!;
    expect(next.setting, setting);
    expect([next.first, next.second]..sort(), [0, 0.5]);

    expect(trials.meanScore(setting, 5, 0), isNull);
    expect(trials.winner(setting, 5), isNull);
    expect(trials.add(setting, 5, 0, 0.5, 0.6, 0.8, 1), isTrue);
    expect(trials.add(setting, 5.5, 0.5, 0, 0.9, 0.7, -1), isTrue);
    expect(trials.add(setting, 5, 0, 0.5, 0.5, 0.5, 0), isTrue);
    expect(trials.add(setting, 5, 0, 9, 0.5, 0.5, 0), isFalse);
    expect([for (var v = 0; v < 2; v++) trials.won(setting, 5, v)], [0, 2]);
    expect([for (var v = 0; v < 2; v++) trials.lost(setting, 5, v)], [2, 0]);
    expect([for (var v = 0; v < 2; v++) trials.tied(setting, 5, v)], [1, 1]);
    expect([for (var v = 0; v < 2; v++) trials.heard(setting, 5, v)], [3, 3]);
    expect(trials.meanScore(setting, 5, 0), closeTo((0.6 + 0.7 + 0.5) / 3, 1e-9));
    expect(trials.winner(setting, 5), isNull);
    trials.setAvailable(setting, false);
    expect(trials.next(5), isNull);
    trials.setConfidence(0.9);
    trials.close();
    expect(() => trials.addSetting(), throwsStateError);
  });

  test('scoreWords aligns the words heard with the sentence', () {
    expect(scoreWords('The cat sat on the mat.', '"the CAT, sat on the mat!"'), const WordScore(1, 6, 0, 0, 0));
    expect(scoreWords('one two three four', 'one too tree four five'), const WordScore(0.5, 2, 0, 2, 1));
    expect(scoreWords('a b', 'b a'), const WordScore(0.5, 1, 1, 0, 1));
    expect(scoreWords("Don't stop", 'don\u2019t stop').share, 1);
    expect(scoreWords('Caf\u00e9 au lait', 'CAF\u00c9 au lait').share, 1);
    expect(scoreWords('hi \u{1F600}x', 'HI \u{1F600}x'), const WordScore(1, 2, 0, 0, 0));
    expect(scoreWords('', ''), const WordScore(1, 0, 0, 0, 0));
    expect(scoreWords('', 'hello'), const WordScore(0, 0, 0, 0, 1));
    expect(scoreWords('hello world', ''), const WordScore(0, 0, 2, 0, 0));
  });
}
