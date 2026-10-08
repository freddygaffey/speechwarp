// Runs inside the real app on a real platform, which is the only way to know the native library was built
// into the app and can be found:
//
//     flutter test integration_test -d macos
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:speechwarp/speechwarp.dart';
import 'package:speechwarp_example/main.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('the native library loads and speeds audio up', (tester) async {
    expect(SpeechwarpStream.libraryVersion, matches(RegExp(r'^\d+\.\d+\.\d+$')));

    final input = testSignal();
    final stream = SpeechwarpStream(sampleRate)
      ..speed = 3
      ..pauseCap = 0.06
      ..rhythmGap = 0.04
      ..write(input)
      ..flush();
    final output = stream.read();
    expect(input.length / output.length, closeTo(3, 0.45));
    expect(stream.position, input.length);
    stream.close();

    // The listener trainer lives in its own source file, which must have been built in too.
    final trainer = ListenerTrainer(seed: 1)..testBegin(10, 0);
    expect(trainer.testRate(), greaterThan(0));
    trainer.close();

    await tester.pumpWidget(const ExampleApp());
    expect(find.textContaining('s out'), findsOneWidget);
  });
}
