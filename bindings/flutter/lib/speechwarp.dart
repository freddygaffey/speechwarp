/// Nonlinear speed-up for speech: listen faster and still follow it.
///
/// ```dart
/// await Speechwarp.initialize();     // once; completes at once except on the web
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

export 'src/common.dart' show TrainerMeasure, TrainerParam, TrainerPlan;
export 'src/ffi.dart' if (dart.library.js_interop) 'src/web.dart';
