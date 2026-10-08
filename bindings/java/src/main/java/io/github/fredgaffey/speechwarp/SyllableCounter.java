package io.github.fredgaffey.speechwarp;

import java.util.OptionalDouble;

/**
 * The syllable-rate estimator behind {@link SpeechwarpStream#syllableRate()}, on its own, for audio that does
 * not go through a stream (a player using some other speed-up, or measuring a file). A counter given the same
 * input as a stream reports the same rate as the stream does with a window of 60 s and a minimum of 10 s.
 * Float input is converted to 16 bits exactly as the stream converts it.
 *
 * <p>Samples are interleaved: a frame is one sample per channel. Offsets and lengths of arrays are in samples.
 *
 * <p>A counter is not thread safe: use it from one thread, or lock around it. {@link #close()} it when done.
 */
public final class SyllableCounter implements AutoCloseable {
    private final int sampleRate;
    private final int channels;
    private long handle;

    static {
        NativeLibrary.ensureLoaded();
    }

    /** Creates a mono counter. */
    public SyllableCounter(int sampleRate) {
        this(sampleRate, 1);
    }

    /**
     * Creates a counter.
     *
     * @param sampleRate samples per second, 4000 to 384000
     * @param channels 1 to 32
     * @throws IllegalArgumentException if the sample rate or channel count is not supported
     */
    public SyllableCounter(int sampleRate, int channels) {
        if (sampleRate < 4000 || sampleRate > 384000) {
            throw new IllegalArgumentException("sampleRate must be from 4000 to 384000, not " + sampleRate);
        }
        if (channels < 1 || channels > 32) {
            throw new IllegalArgumentException("channels must be from 1 to 32, not " + channels);
        }
        handle = nativeCreate(sampleRate, channels);
        if (handle == 0) {
            throw new OutOfMemoryError("speechwarp could not allocate a syllable counter");
        }
        this.sampleRate = sampleRate;
        this.channels = channels;
    }

    public int sampleRate() {
        return sampleRate;
    }

    /** Samples in a frame. */
    public int channels() {
        return channels;
    }

    /** Adds input: the whole array, which must hold a whole number of frames. */
    public void write(float[] samples) {
        write(samples, 0, samples.length);
    }

    /**
     * Adds input.
     *
     * @param length the number of samples, which must be a whole number of frames
     */
    public void write(float[] samples, int offset, int length) {
        if (!nativeWriteFloat(open(), samples, offset, wholeFrames(samples.length, offset, length))) {
            throw new OutOfMemoryError();
        }
    }

    /** Adds input: the whole array, which must hold a whole number of frames. */
    public void write(short[] samples) {
        write(samples, 0, samples.length);
    }

    /** @see #write(float[], int, int) */
    public void write(short[] samples, int offset, int length) {
        if (!nativeWriteShort(open(), samples, offset, wholeFrames(samples.length, offset, length))) {
            throw new OutOfMemoryError();
        }
    }

    /** Syllables a second over the last 60 s written, or empty until 10 s have been written. */
    public OptionalDouble rate() {
        return rate(60, 10);
    }

    /**
     * Syllables a second over the last {@code windowSeconds} written (or all of it, if less), or empty until
     * {@code minimumSeconds} have been written. The window is clamped to 1 to 120 s, the minimum to 0 up to the
     * window. Multiply by the speed for the rate heard.
     */
    public OptionalDouble rate(double windowSeconds, double minimumSeconds) {
        double rate = nativeRate(open(), windowSeconds, minimumSeconds);
        return rate < 0 ? OptionalDouble.empty() : OptionalDouble.of(rate);
    }

    /** Forgets everything written. */
    public void reset() {
        nativeReset(open());
    }

    /** Frees the native counter. Using it afterwards throws {@link IllegalStateException}. */
    @Override
    public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private long open() {
        if (handle == 0) {
            throw new IllegalStateException("the syllable counter is closed");
        }
        return handle;
    }

    private int wholeFrames(int size, int offset, int length) {
        if (offset < 0 || length < 0 || offset > size - length) {
            throw new IndexOutOfBoundsException(
                    "offset " + offset + " and length " + length + " do not fit an array of " + size);
        }
        if (length % channels != 0) {
            throw new IllegalArgumentException(
                    length + " samples is not a whole number of " + channels + "-channel frames");
        }
        return length / channels;
    }

    private static native long nativeCreate(int sampleRate, int channels);
    private static native void nativeDestroy(long handle);
    private static native boolean nativeWriteFloat(long handle, float[] samples, int offset, int frames);
    private static native boolean nativeWriteShort(long handle, short[] samples, int offset, int frames);
    private static native double nativeRate(long handle, double windowSeconds, double minimumSeconds);
    private static native void nativeReset(long handle);
}
