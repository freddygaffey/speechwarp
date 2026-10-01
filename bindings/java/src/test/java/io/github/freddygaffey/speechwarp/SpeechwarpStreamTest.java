package io.github.freddygaffey.speechwarp;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
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
}
