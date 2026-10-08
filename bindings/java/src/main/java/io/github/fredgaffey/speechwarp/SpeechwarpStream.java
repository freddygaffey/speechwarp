package io.github.fredgaffey.speechwarp;

import java.util.OptionalDouble;

/**
 * Speeds up speech. Write audio in, read the faster audio out.
 *
 * <p>Samples are interleaved: a frame is one sample per channel. Floats are in the range -1 to 1. Offsets and
 * lengths of arrays are in samples, as usual for arrays; what {@code read} returns, {@link
 * #framesAvailable()} and {@link #position()} are in frames.
 *
 * <p>A stream is not thread safe: use it from one thread, or lock around it. {@link #close()} it when done.
 *
 * <p>This is the desktop counterpart of the Android library's class of the same name, and shares its native
 * code.
 */
public final class SpeechwarpStream implements AutoCloseable {
    /** The slowest speed that can be set. */
    public static final float MIN_SPEED = 0.05f;

    /** The fastest speed that can be set. */
    public static final float MAX_SPEED = 20f;

    private final int sampleRate;
    private final int channels;
    private long handle;

    static {
        NativeLibrary.ensureLoaded();
    }

    /** Creates a mono stream at speed 1 with nonlinear speed-up on. */
    public SpeechwarpStream(int sampleRate) {
        this(sampleRate, 1);
    }

    /**
     * Creates a stream at speed 1 with nonlinear speed-up on.
     *
     * @param sampleRate samples per second, 4000 to 384000
     * @param channels 1 to 32
     * @throws IllegalArgumentException if the sample rate or channel count is not supported
     */
    public SpeechwarpStream(int sampleRate, int channels) {
        if (sampleRate < 4000 || sampleRate > 384000) {
            throw new IllegalArgumentException("sampleRate must be from 4000 to 384000, not " + sampleRate);
        }
        if (channels < 1 || channels > 32) {
            throw new IllegalArgumentException("channels must be from 1 to 32, not " + channels);
        }
        handle = nativeCreate(sampleRate, channels);
        if (handle == 0) {
            throw new OutOfMemoryError("speechwarp could not allocate a stream");
        }
        this.sampleRate = sampleRate;
        this.channels = channels;
    }

    /** The version of the native library, such as "0.3.4". */
    public static String libraryVersion() {
        return nativeVersion();
    }

    public int sampleRate() {
        return sampleRate;
    }

    /** Samples in a frame. */
    public int channels() {
        return channels;
    }

    public float speed() {
        return nativeGetSpeed(open());
    }

    /**
     * Sets the overall speed: 2 plays twice as fast. Clamped to {@link #MIN_SPEED}..{@link #MAX_SPEED}.
     *
     * <p>Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With
     * nonlinear speed-up the speed varies from moment to moment and its average is steered to this value;
     * expect the result within a few percent.
     *
     * @throws IllegalArgumentException if the speed is zero, negative or not a number
     */
    public void setSpeed(float speed) {
        if (!(speed > 0)) {
            throw new IllegalArgumentException("speed must be greater than zero, not " + speed);
        }
        nativeSetSpeed(open(), speed);
    }

    public float nonlinear() {
        return nativeGetNonlinear(open());
    }

    /**
     * Sets how unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
     * pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
     */
    public void setNonlinear(float amount) {
        if (Float.isNaN(amount)) {
            throw new IllegalArgumentException("nonlinear must be a number");
        }
        nativeSetNonlinear(open(), amount);
    }

    // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
    // docs/how-it-works.md.

    public float pauseCap() {
        return nativeGetPauseCap(open());
    }

    /**
     * Shortens every pause to at most this many seconds of input before speeding up, so that the speed is
     * spent on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. {@link
     * #position()} counts the frames left out.
     *
     * @throws IllegalArgumentException if the value is not a number
     */
    public void setPauseCap(float seconds) {
        nativeSetPauseCap(open(), number("pauseCap", seconds));
    }

    public boolean keepSpeed() {
        return nativeGetKeepSpeed(open());
    }

    /**
     * While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses
     * is spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them
     * faster. When false, trimmed pauses make playback faster than the speed.
     */
    public void setKeepSpeed(boolean enabled) {
        nativeSetKeepSpeed(open(), enabled);
    }

    public float speedFloor() {
        return nativeGetSpeedFloor(open());
    }

    /**
     * No 10 ms block of speech plays slower than this fraction of the speed: at 8x, 0.5 keeps every block at
     * 4x or more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with
     * nonlinear speed-up above 1x.
     *
     * @throws IllegalArgumentException if the value is not a number
     */
    public void setSpeedFloor(float fraction) {
        nativeSetSpeedFloor(open(), number("speedFloor", fraction));
    }

    public float rhythmGap() {
        return nativeGetRhythmGap(open());
    }

    /**
     * Seconds of silence put into the output {@link #rhythmRate()} times a second, at the quietest point
     * nearby, which can help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02
     * to 0.06. Allowed: 0, or 0.005 to 0.2. {@link #position()} holds still during a gap.
     *
     * @throws IllegalArgumentException if the value is not a number
     */
    public void setRhythmGap(float seconds) {
        nativeSetRhythmGap(open(), number("rhythmGap", seconds));
    }

    public float rhythmRate() {
        return nativeGetRhythmRate(open());
    }

    /**
     * Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8.
     *
     * @throws IllegalArgumentException if the value is zero, negative or not a number
     */
    public void setRhythmRate(float perSecond) {
        if (!(perSecond > 0)) {
            throw new IllegalArgumentException("rhythmRate must be greater than zero, not " + perSecond);
        }
        nativeSetRhythmRate(open(), perSecond);
    }

    public float heardPause() {
        return nativeGetHeardPause(open());
    }

    public float heardPauseFrom() {
        return nativeGetHeardPauseFrom(open());
    }

    /**
     * Heard pause: keeps each pause about {@code seconds} long in the output, by setting the pause cap to
     * {@code seconds} times the current speed (clamped to 0.03 to 0.4 s of input) whenever the speed changes.
     * Below {@code fromSpeed} pauses are left alone. {@code seconds}: 0 turns the rule off (and the pause cap
     * with it); otherwise 0.002 to 0.4. {@code fromSpeed}: 1 to 20. Sensible: 0.015 to 0.06 heard, from 3x.
     * Calling {@link #setPauseCap(float)} turns the rule off ({@link #heardPause()} then returns 0), and {@link
     * #pauseCap()} always returns the value in force.
     *
     * @throws IllegalArgumentException if an argument is not a number
     */
    public void setHeardPause(float seconds, float fromSpeed) {
        nativeSetHeardPause(open(), number("seconds", seconds), number("fromSpeed", fromSpeed));
    }

    public float floorBlend() {
        return nativeGetFloorBlend(open());
    }

    public float floorBlendFrom() {
        return nativeGetFloorBlendFrom(open());
    }

    public float floorBlendFull() {
        return nativeGetFloorBlendFull(open());
    }

    /**
     * Floor blend: the speed floor in force is 0 below {@code fromSpeed}, rises linearly to {@code fraction}
     * at {@code fullSpeed}, and stays at {@code fraction} above it, so that a speed ramp never changes the
     * sound in a jump. {@code fraction}: 0 turns the rule off (and the floor with it); otherwise up to 1.
     * Speeds 1 to 20; if {@code fullSpeed} is not above {@code fromSpeed} it is taken as equal, and the floor
     * steps to {@code fraction} at {@code fromSpeed}. Sensible: 0.5 from 4x, full at 6x. Calling {@link
     * #setSpeedFloor(float)} turns the rule off ({@link #floorBlend()} then returns 0), and {@link
     * #speedFloor()} always returns the value in force.
     *
     * @throws IllegalArgumentException if an argument is not a number
     */
    public void setFloorBlend(float fraction, float fromSpeed, float fullSpeed) {
        nativeSetFloorBlend(open(), number("fraction", fraction), number("fromSpeed", fromSpeed),
                number("fullSpeed", fullSpeed));
    }

    /**
     * Syllables a second in the input, pauses included, over about the last 60 s written; empty until 10 s
     * have been written since creation or {@link #reset()}. Multiply by the speed for the rate heard. An
     * estimate, typically within about 10%.
     */
    public OptionalDouble syllableRate() {
        double rate = nativeSyllableRate(open());
        return rate < 0 ? OptionalDouble.empty() : OptionalDouble.of(rate);
    }

    private static float number(String name, float value) {
        if (Float.isNaN(value)) {
            throw new IllegalArgumentException(name + " must be a number");
        }
        return value;
    }

    /** Frames of output ready to read. */
    public int framesAvailable() {
        return nativeAvailable(open());
    }

    /**
     * The input frame, counted from creation or the last {@link #reset()}, that the next output frame to be
     * read was made from. This is how a player maps what is being heard back to a place in the source.
     *
     * <p>It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after
     * a {@link #flush()} has been read it equals the number of frames written.
     */
    public long position() {
        return nativePosition(open());
    }

    /** Adds input: the whole array, which must hold a whole number of frames. */
    public void write(float[] samples) {
        write(samples, 0, samples.length);
    }

    /**
     * Adds input. The output does not depend on how the input is divided between calls.
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

    /** Takes processed output into the whole array. */
    public int read(float[] samples) {
        return read(samples, 0, samples.length);
    }

    /**
     * Takes processed output, as many whole frames as fit in {@code length} samples.
     *
     * @return the number of frames written, which is the number of samples divided by {@link #channels()}. It
     *     may be 0: output lags input by a short look-ahead.
     */
    public int read(float[] samples, int offset, int length) {
        checkRange(samples.length, offset, length);
        return nativeReadFloat(open(), samples, offset, length / channels);
    }

    /** Takes processed output into the whole array. */
    public int read(short[] samples) {
        return read(samples, 0, samples.length);
    }

    /** @see #read(float[], int, int) */
    public int read(short[] samples, int offset, int length) {
        checkRange(samples.length, offset, length);
        return nativeReadShort(open(), samples, offset, length / channels);
    }

    /**
     * Processes everything written so far, at the end of the input. Read until empty afterwards. Writing more
     * starts a new stretch of audio, and {@link #position()} carries on counting.
     */
    public void flush() {
        if (!nativeFlush(open())) {
            throw new OutOfMemoryError();
        }
    }

    /**
     * Discards all buffered input and output, keeping the speed and nonlinear settings, and starts {@link
     * #position()} again from zero. Use after seeking.
     */
    public void reset() {
        nativeReset(open());
    }

    /** Frees the native stream. Using the stream afterwards throws {@link IllegalStateException}. */
    @Override
    public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private long open() {
        if (handle == 0) {
            throw new IllegalStateException("the stream is closed");
        }
        return handle;
    }

    private void checkRange(int size, int offset, int length) {
        if (offset < 0 || length < 0 || offset > size - length) {
            throw new IndexOutOfBoundsException(
                    "offset " + offset + " and length " + length + " do not fit an array of " + size);
        }
    }

    private int wholeFrames(int size, int offset, int length) {
        checkRange(size, offset, length);
        if (length % channels != 0) {
            throw new IllegalArgumentException(
                    length + " samples is not a whole number of " + channels + "-channel frames");
        }
        return length / channels;
    }

    private static native String nativeVersion();
    private static native long nativeCreate(int sampleRate, int channels);
    private static native void nativeDestroy(long handle);
    private static native void nativeSetSpeed(long handle, float speed);
    private static native float nativeGetSpeed(long handle);
    private static native void nativeSetNonlinear(long handle, float amount);
    private static native float nativeGetNonlinear(long handle);
    private static native void nativeSetPauseCap(long handle, float value);
    private static native float nativeGetPauseCap(long handle);
    private static native void nativeSetKeepSpeed(long handle, boolean enabled);
    private static native boolean nativeGetKeepSpeed(long handle);
    private static native void nativeSetSpeedFloor(long handle, float value);
    private static native float nativeGetSpeedFloor(long handle);
    private static native void nativeSetRhythmGap(long handle, float value);
    private static native float nativeGetRhythmGap(long handle);
    private static native void nativeSetRhythmRate(long handle, float value);
    private static native float nativeGetRhythmRate(long handle);
    private static native void nativeSetHeardPause(long handle, float seconds, float fromSpeed);
    private static native float nativeGetHeardPause(long handle);
    private static native float nativeGetHeardPauseFrom(long handle);
    private static native void nativeSetFloorBlend(long handle, float fraction, float fromSpeed, float fullSpeed);
    private static native float nativeGetFloorBlend(long handle);
    private static native float nativeGetFloorBlendFrom(long handle);
    private static native float nativeGetFloorBlendFull(long handle);
    private static native double nativeSyllableRate(long handle);
    private static native int nativeAvailable(long handle);
    private static native long nativePosition(long handle);
    private static native boolean nativeFlush(long handle);
    private static native void nativeReset(long handle);
    private static native boolean nativeWriteFloat(long handle, float[] samples, int offset, int frames);
    private static native boolean nativeWriteShort(long handle, short[] samples, int offset, int frames);
    private static native int nativeReadFloat(long handle, float[] samples, int offset, int maxFrames);
    private static native int nativeReadShort(long handle, short[] samples, int offset, int maxFrames);
}
