package io.github.freddygaffey.speechwarp;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.nio.file.Files;
import java.nio.file.Paths;
import org.junit.Test;

public class SpeechwarpStreamTest {
    private static final int RATE = 22050;

    /** Something for the library to chew on: a gliding tone in bursts, with gaps. */
    private static float[] signal(double seconds, int channels) {
        int frames = (int) (RATE * seconds);
        float[] samples = new float[frames * channels];
        double phase = 0;
        for (int i = 0; i < frames; i++) {
            double t = (double) i / RATE;
            phase += 2 * Math.PI * (120 + 60 * Math.sin(t * 3)) / RATE;
            double envelope = t % 0.4 < 0.3 ? Math.sin(Math.PI * (t % 0.4) / 0.3) : 0;
            float value = (float) (0.4 * envelope
                    * (Math.sin(phase) + 0.5 * Math.sin(2 * phase) + 0.3 * Math.sin(3 * phase)));
            for (int c = 0; c < channels; c++) {
                samples[i * channels + c] = value;
            }
        }
        return samples;
    }

    private static float[] readAll(SpeechwarpStream stream) {
        float[] out = new float[stream.framesAvailable() * stream.channels()];
        assertEquals(out.length / stream.channels(), stream.read(out));
        return out;
    }

    private static float[] speedUp(float[] input, float speed, float nonlinear) {
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            stream.setSpeed(speed);
            stream.setNonlinear(nonlinear);
            stream.write(input);
            stream.flush();
            return readAll(stream);
        }
    }

    @Test
    public void versionMatchesTheHeader() throws Exception {
        String header = new String(Files.readAllBytes(Paths.get(System.getProperty("speechwarp.header"))));
        assertTrue(header.contains("#define SPEECHWARP_VERSION \"" + SpeechwarpStream.libraryVersion() + "\""));
    }

    @Test
    public void defaultsAndArguments() {
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE, 2)) {
            assertEquals(1f, stream.speed(), 0);
            assertEquals(1f, stream.nonlinear(), 0);
            assertEquals(0, stream.framesAvailable());
            assertEquals(0L, stream.position());
            assertEquals(RATE, stream.sampleRate());
            assertEquals(2, stream.channels());

            assertThrows(IllegalArgumentException.class, () -> stream.setSpeed(0));
            assertThrows(IllegalArgumentException.class, () -> stream.setSpeed(Float.NaN));
            assertThrows(IllegalArgumentException.class, () -> stream.setNonlinear(Float.NaN));
            assertThrows(IllegalArgumentException.class, () -> stream.write(new float[3]));
            assertThrows(IndexOutOfBoundsException.class, () -> stream.write(new float[4], 2, 4));
            assertThrows(IndexOutOfBoundsException.class, () -> stream.read(new short[4], -1, 2));

            stream.setSpeed(1000);
            assertEquals(SpeechwarpStream.MAX_SPEED, stream.speed(), 0);
            stream.setNonlinear(-3);
            assertEquals(0f, stream.nonlinear(), 0);
        }
        assertThrows(IllegalArgumentException.class, () -> new SpeechwarpStream(100));
        assertThrows(IllegalArgumentException.class, () -> new SpeechwarpStream(RATE, 0));
    }

    @Test
    public void outputIsShorterByTheSpeed() {
        float[] input = signal(20, 1);
        float[][] cases = {{1, 1}, {3, 1}, {3, 0}, {8, 1}};
        for (float[] c : cases) {
            float[] output = speedUp(input, c[0], c[1]);
            assertEquals(c[0], (float) input.length / output.length, 0.1f * c[0]);
        }
    }

    @Test
    public void stereoShortsAndOffsets() {
        float[] input = signal(5, 2);
        short[] shorts = new short[input.length + 4];
        for (int i = 0; i < input.length; i++) {
            shorts[i + 4] = (short) (input[i] * 32767);
        }
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE, 2)) {
            stream.setSpeed(2);
            stream.write(shorts, 4, input.length);
            stream.flush();

            short[] buffer = new short[10 + 1000 * 2 + 1]; // one sample too many: only whole frames are written
            int total = 0;
            while (true) {
                buffer[9] = 777;
                int frames = stream.read(buffer, 10, buffer.length - 10);
                if (frames == 0) {
                    break;
                }
                assertTrue(frames <= 1000);
                assertEquals(777, buffer[9]);
                for (int i = 0; i < frames; i++) {
                    assertEquals(buffer[10 + 2 * i], buffer[10 + 2 * i + 1]);
                }
                total += frames;
            }
            assertEquals(2.0, (double) (input.length / 2) / total, 0.2);
        }
    }

    @Test
    public void positionFollowsWhatIsRead() {
        float[] input = signal(20, 1);
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            stream.setSpeed(4);
            float[] buffer = new float[512];
            int written = 0;
            long last = 0;
            while (true) {
                if (written < input.length && stream.framesAvailable() < 512) {
                    int n = Math.min(2000, input.length - written);
                    stream.write(input, written, n);
                    written += n;
                    if (written == input.length) {
                        stream.flush();
                    }
                    continue;
                }
                long position = stream.position();
                assertTrue(position >= last && position <= written);
                last = position;
                if (stream.read(buffer) == 0) {
                    break;
                }
            }
            assertEquals(input.length, stream.position());
        }
    }

    @Test
    public void resetDiscardsEverything() {
        float[] input = signal(5, 1);
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            stream.setSpeed(3);
            stream.write(input);
            assertTrue(stream.framesAvailable() > 0);
            stream.reset();
            assertEquals(0, stream.framesAvailable());
            assertEquals(0L, stream.position());
            assertEquals(3f, stream.speed(), 0);
            stream.write(input);
            stream.flush();
            assertArrayEquals(speedUp(input, 3, 1), readAll(stream), 0f);
        }
    }

    @Test
    public void aClosedStreamThrows() {
        SpeechwarpStream stream = new SpeechwarpStream(RATE);
        stream.close();
        stream.close();
        assertThrows(IllegalStateException.class, () -> stream.setSpeed(2));
        assertThrows(IllegalStateException.class, () -> stream.write(new float[10]));
    }

    @Test
    public void highSpeedOptionsStartOffAndClamp() {
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            assertEquals(0f, stream.pauseCap(), 0);
            assertTrue(stream.keepSpeed());
            assertEquals(0f, stream.speedFloor(), 0);
            assertEquals(0f, stream.rhythmGap(), 0);
            assertEquals(5f, stream.rhythmRate(), 0);
            assertFalse(stream.syllableRate().isPresent());
            stream.setPauseCap(5);
            stream.setSpeedFloor(0.5f);
            stream.setRhythmGap(0.04f);
            stream.setRhythmRate(100);
            stream.setKeepSpeed(false);
            assertEquals(1f, stream.pauseCap(), 0);
            assertEquals(0.5f, stream.speedFloor(), 0);
            assertEquals(0.04f, stream.rhythmGap(), 1e-6f);
            assertEquals(16f, stream.rhythmRate(), 0);
            assertFalse(stream.keepSpeed());
            assertThrows(IllegalArgumentException.class, () -> stream.setPauseCap(Float.NaN));
            assertThrows(IllegalArgumentException.class, () -> stream.setRhythmRate(0));
        }
    }

    @Test
    public void pauseCapShortensAndPositionReachesTheEnd() {
        float[] input = signal(12, 1);
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            stream.setNonlinear(0);
            stream.setPauseCap(0.03f);
            stream.setKeepSpeed(false);
            stream.write(input);
            stream.flush();
            assertTrue(readAll(stream).length < input.length * 0.9);
            assertEquals(input.length, stream.position());
            assertTrue(stream.syllableRate().isPresent());
        }
    }

    @Test
    public void heardPauseAndFloorBlendFollowTheSpeed() {
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE)) {
            assertEquals(0f, stream.heardPause(), 0);
            assertEquals(0f, stream.floorBlend(), 0);

            stream.setHeardPause(0.03f, 3f);
            assertEquals(0.03f, stream.heardPause(), 1e-6f);
            assertEquals(3f, stream.heardPauseFrom(), 0);
            stream.setSpeed(2f);
            assertEquals(0f, stream.pauseCap(), 0);
            stream.setSpeed(5f);
            assertEquals(0.15f, stream.pauseCap(), 1e-6f);
            stream.setSpeed(20f);
            assertEquals(0.4f, stream.pauseCap(), 1e-6f); // clamped to 0.4 s of input

            stream.setFloorBlend(0.5f, 4f, 6f);
            assertEquals(0.5f, stream.floorBlend(), 0);
            assertEquals(4f, stream.floorBlendFrom(), 0);
            assertEquals(6f, stream.floorBlendFull(), 0);
            stream.setSpeed(3f);
            assertEquals(0f, stream.speedFloor(), 0);
            stream.setSpeed(5f);
            assertEquals(0.25f, stream.speedFloor(), 1e-6f);
            stream.setSpeed(8f);
            assertEquals(0.5f, stream.speedFloor(), 1e-6f);

            // Setting the fixed options turns the rules off: the speed no longer moves them.
            stream.setPauseCap(0.1f);
            stream.setSpeedFloor(0.2f);
            stream.setSpeed(6f);
            assertEquals(0.1f, stream.pauseCap(), 1e-6f);
            assertEquals(0.2f, stream.speedFloor(), 1e-6f);
            assertEquals(0f, stream.heardPause(), 0);
            assertEquals(0f, stream.floorBlend(), 0);

            stream.setHeardPause(0f, 3f);
            assertEquals(0f, stream.heardPause(), 0);
            assertThrows(IllegalArgumentException.class, () -> stream.setHeardPause(Float.NaN, 3f));
            assertThrows(IllegalArgumentException.class, () -> stream.setFloorBlend(0.5f, Float.NaN, 6f));
        }
    }

    @Test
    public void syllableCounterMatchesTheStream() {
        float[] input = signal(25, 1);
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE); SyllableCounter counter = new SyllableCounter(RATE)) {
            assertFalse(counter.rate().isPresent());
            int head = RATE * 5;
            stream.write(input, 0, head);
            counter.write(input, 0, head);
            assertFalse(stream.syllableRate().isPresent());
            assertFalse(counter.rate().isPresent());

            stream.write(input, head, input.length - head);
            counter.write(input, head, input.length - head);
            double expected = stream.syllableRate().getAsDouble();
            assertTrue(expected > 0);
            assertEquals(expected, counter.rate().getAsDouble(), 0);
            assertFalse(counter.rate(120, 40).isPresent()); // 25 s written, 40 s needed
            assertTrue(counter.rate(60, 0).isPresent());

            counter.reset();
            assertFalse(counter.rate().isPresent());
        }
    }

    @Test
    public void syllableCounterTakesShortsAndChecksArguments() {
        float[] input = signal(12, 2);
        short[] shorts = new short[input.length];
        for (int i = 0; i < input.length; i++) {
            shorts[i] = (short) (input[i] * 32767);
        }
        try (SpeechwarpStream stream = new SpeechwarpStream(RATE, 2); SyllableCounter counter = new SyllableCounter(RATE, 2)) {
            stream.write(shorts);
            counter.write(shorts);
            assertEquals(stream.syllableRate().getAsDouble(), counter.rate().getAsDouble(), 0);
            assertThrows(IllegalArgumentException.class, () -> counter.write(new float[3]));
            assertThrows(IndexOutOfBoundsException.class, () -> counter.write(shorts, 2, shorts.length));
        }
        assertThrows(IllegalArgumentException.class, () -> new SyllableCounter(100));
        assertThrows(IllegalArgumentException.class, () -> new SyllableCounter(RATE, 33));
        SyllableCounter counter = new SyllableCounter(RATE);
        counter.close();
        counter.close();
        assertThrows(IllegalStateException.class, counter::rate);
    }
}
