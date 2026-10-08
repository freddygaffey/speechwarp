// A small app that proves the plugin works on the device it runs on: it makes a test signal, speeds it up,
// and shows what came out. It plays no sound. To play audio, hand what SpeechwarpStream.read() returns to an
// audio output package, and do the work in an isolate if it would hold up the UI (on the web there are no
// isolates to use here; the work runs on the page's thread).
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:speechwarp/speechwarp.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // On the web this loads the WebAssembly, so it must be waited for; everywhere else it completes at once.
  await Speechwarp.initialize();
  runApp(const ExampleApp());
}

const sampleRate = 44100;

/// Ten seconds of a warbling tone in bursts, standing in for speech.
Float32List testSignal() {
  final samples = Float32List(sampleRate * 10);
  for (var i = 0; i < samples.length; i++) {
    final t = i / sampleRate;
    samples[i] = t % 0.4 < 0.3 ? 0.3 * sin(2 * pi * 150 * t + 3 * sin(2 * pi * 2 * t)) : 0;
  }
  return samples;
}

/// Speeds the signal up and describes the result.
String run(double speed, bool nonlinear) {
  final input = testSignal();
  final watch = Stopwatch()..start();
  final stream = SpeechwarpStream(sampleRate)
    ..speed = speed
    ..nonlinear = nonlinear ? 1 : 0
    ..write(input)
    ..flush();
  final output = stream.read();
  final position = stream.position;
  stream.close();
  watch.stop();

  final secondsIn = input.length / sampleRate;
  final secondsOut = output.length / sampleRate;
  return '${secondsIn.toStringAsFixed(1)} s in, ${secondsOut.toStringAsFixed(1)} s out: '
      '${(secondsIn / secondsOut).toStringAsFixed(2)}x\n'
      'position at the end: $position of ${input.length} frames\n'
      'took ${watch.elapsedMilliseconds} ms';
}

class ExampleApp extends StatefulWidget {
  const ExampleApp({super.key});

  @override
  State<ExampleApp> createState() => _ExampleAppState();
}

class _ExampleAppState extends State<ExampleApp> {
  double speed = 3;
  bool nonlinear = true;
  String result = '';

  @override
  void initState() {
    super.initState();
    result = run(speed, nonlinear);
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: Text('speechwarp ${SpeechwarpStream.libraryVersion}')),
        body: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Speed ${speed.toStringAsFixed(1)}x'),
              Slider(
                min: 0.5,
                max: 10,
                value: speed,
                onChanged: (value) => setState(() => speed = value),
                onChangeEnd: (_) => setState(() => result = run(speed, nonlinear)),
              ),
              SwitchListTile(
                title: const Text('Nonlinear'),
                value: nonlinear,
                onChanged: (value) => setState(() {
                  nonlinear = value;
                  result = run(speed, nonlinear);
                }),
              ),
              const SizedBox(height: 16),
              Text(result, key: const Key('result')),
            ],
          ),
        ),
      ),
    );
  }
}
