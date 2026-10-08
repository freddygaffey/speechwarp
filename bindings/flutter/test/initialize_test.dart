// What happens before Speechwarp.initialize has completed. Its own file, because a test file shares one page
// (or isolate) and the other tests initialise first.
import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_test/flutter_test.dart';
import 'package:speechwarp/speechwarp.dart';

void main() {
  test('on the web, nothing works until initialize has completed, and calls share one load', () async {
    if (!kIsWeb) {
      // Native code needs no waiting: it is ready from the start.
      expect(Speechwarp.isInitialized, isTrue);
      await Speechwarp.initialize();
      return;
    }
    expect(Speechwarp.isInitialized, isFalse);
    expect(() => SpeechwarpStream(44100), throwsStateError);
    expect(() => SyllableCounter(44100), throwsStateError);
    expect(() => ListenerTrainer(), throwsStateError);
    expect(() => BlindTrials(), throwsStateError);
    expect(() => SpeechwarpStream.libraryVersion, throwsStateError);
    // Bad arguments are still reported as such.
    expect(() => SpeechwarpStream(100), throwsRangeError);

    final first = Speechwarp.initialize();
    final second = Speechwarp.initialize();
    expect(identical(first, second), isTrue);
    await first;
    expect(Speechwarp.isInitialized, isTrue);
    expect(SpeechwarpStream.libraryVersion, isNotEmpty);
    SpeechwarpStream(44100).close();
  });
}
