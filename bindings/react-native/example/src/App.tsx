// Proves the module works on the device it runs on: it makes a test signal, speeds it up, and shows what
// came out. It plays no sound.
import { useState } from 'react';
import { Button, StyleSheet, Text, View } from 'react-native';
import { Stream, version } from 'react-native-speechwarp';

const SAMPLE_RATE = 44100;

/** Ten seconds of a warbling tone in bursts, standing in for speech. */
function testSignal(): Float32Array {
  const samples = new Float32Array(SAMPLE_RATE * 10);
  for (let i = 0; i < samples.length; i++) {
    const t = i / SAMPLE_RATE;
    samples[i] = t % 0.4 < 0.3 ? 0.3 * Math.sin(2 * Math.PI * 150 * t + 3 * Math.sin(2 * Math.PI * 2 * t)) : 0;
  }
  return samples;
}

function run(speed: number, nonlinear: boolean): string {
  try {
    const input = testSignal();
    const started = Date.now();
    const stream = new Stream(SAMPLE_RATE);
    stream.speed = speed;
    stream.nonlinear = nonlinear ? 1 : 0;
    // In pieces, as a player would feed it.
    let frames = 0;
    for (let at = 0; at < input.length; at += 8192) {
      stream.write(input.subarray(at, at + 8192));
      frames += stream.read().length;
    }
    stream.flush();
    const tail = stream.read();
    frames += tail.length;
    const position = stream.position;
    stream.free();

    const ok = Math.abs(input.length / frames / speed - 1) < 0.1 && position === input.length;
    return [
      ok ? 'SPEECHWARP OK' : 'SPEECHWARP WRONG',
      `${speed}x ${nonlinear ? 'nonlinear' : 'even'}`,
      `${(input.length / SAMPLE_RATE).toFixed(1)} s in, ${(frames / SAMPLE_RATE).toFixed(1)} s out: ` +
        `${(input.length / frames).toFixed(2)}x`,
      `position at the end: ${position} of ${input.length}`,
      `took ${Date.now() - started} ms`,
    ].join('\n');
  } catch (error) {
    return `SPEECHWARP FAILED\n${error}`;
  }
}

export default function App() {
  const [nonlinear, setNonlinear] = useState(true);
  const [speed, setSpeed] = useState(3);
  return (
    <View style={styles.container}>
      <Text style={styles.title}>speechwarp {version()}</Text>
      <Text style={styles.result}>{run(speed, nonlinear)}</Text>
      <Button title="Faster" onPress={() => setSpeed(Math.min(10, speed + 1))} />
      <Button title="Slower" onPress={() => setSpeed(Math.max(1, speed - 1))} />
      <Button title={nonlinear ? 'Switch to even' : 'Switch to nonlinear'} onPress={() => setNonlinear(!nonlinear)} />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, alignItems: 'center', justifyContent: 'center', padding: 24 },
  title: { fontSize: 22, marginBottom: 16 },
  result: { fontSize: 16, marginBottom: 24, textAlign: 'center' },
});
