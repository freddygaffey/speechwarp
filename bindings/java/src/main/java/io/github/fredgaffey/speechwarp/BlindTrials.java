package io.github.fredgaffey.speechwarp;

import java.util.Optional;
import java.util.OptionalDouble;
import java.util.OptionalInt;

/**
 * Designs the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
 * values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
 * caller names the settings; here they are numbers (0, 1, ... in the order added). Deterministic for a given
 * seed.
 *
 * <p>Not thread safe. {@link #close()} it when done.
 */
public final class BlindTrials implements AutoCloseable {
    private long handle;

    static {
        NativeLibrary.ensureLoaded();
    }

    /** Creates trials with seed 0. */
    public BlindTrials() {
        this(0);
    }

    /**
     * Creates trials.
     *
     * @param seed the seed for the random choices, as the bit pattern of an unsigned 64-bit number
     */
    public BlindTrials(long seed) {
        handle = nativeCreate(seed);
        if (handle == 0) {
            throw new OutOfMemoryError("speechwarp could not allocate blind trials");
        }
    }

    /** Adds a setting. Returns its number (0, 1, ...), or -1. */
    public int addSetting() {
        return nativeAddSetting(open());
    }

    /** Adds a value to compare to a setting. Returns its number within the setting, or -1 (duplicate, bad setting). */
    public int addValue(int setting, double value) {
        return nativeAddValue(open(), setting, value);
    }

    /** Leaves a setting out of {@link #next} while false (say, when its method is not available). Default true. */
    public void setAvailable(int setting, boolean available) {
        nativeSetAvailable(open(), setting, available);
    }

    /**
     * Records a trial at {@code speed}: the two values in the order heard, each one's score 0..1, and {@code
     * preferred}: -1 the first, 1 the second, 0 neither. Values must be ones added.
     *
     * @return false if an argument is invalid
     */
    public boolean add(int setting, double speed, double firstValue, double secondValue, double firstScore,
            double secondScore, int preferred) {
        return nativeAdd(open(), setting, speed, firstValue, secondValue, firstScore, secondScore, preferred);
    }

    /**
     * Chooses the next trial at {@code speed}: the available setting with the fewest trials in its band (ties at
     * random), its pair of values compared least (ties at random), in random order. Empty if no setting has two
     * values.
     */
    public Optional<BlindTrial> next(double speed) {
        long trials = open();
        int setting = nativeNext(trials, speed);
        if (setting < 0) {
            return Optional.empty();
        }
        return Optional.of(new BlindTrial(setting, nativeNextFirst(trials), nativeNextSecond(trials)));
    }

    /** Comparisons a value has won in the band of {@code speed}; {@code value} is its number within the setting. */
    public int won(int setting, double speed, int value) {
        return nativeWon(open(), setting, speed, value);
    }

    /** Comparisons a value has lost in the band of {@code speed}. */
    public int lost(int setting, double speed, int value) {
        return nativeLost(open(), setting, speed, value);
    }

    /** Comparisons a value has tied in the band of {@code speed}. */
    public int tied(int setting, double speed, int value) {
        return nativeTied(open(), setting, speed, value);
    }

    /** Trials a value was heard in, in the band of {@code speed}. */
    public int heard(int setting, double speed, int value) {
        return nativeHeard(open(), setting, speed, value);
    }

    /** A value's mean score in the band of {@code speed}; empty if never heard. */
    public OptionalDouble meanScore(int setting, double speed, int value) {
        double score = nativeMeanScore(open(), setting, speed, value);
        return Double.isNaN(score) ? OptionalDouble.empty() : OptionalDouble.of(score);
    }

    /**
     * The value (its number within the setting) with a reliable win in the band of {@code speed}, or empty. A
     * value wins when it has been heard in at least 5 trials, has met every other value in at least 3, and
     * against each the Bayes factor for "preferred" over "no preference" is at least 1 / (1 - confidence): 20 at
     * the default 0.95. However often this is asked, the chance of ever naming a winner between two values that
     * are really alike is at most 1 - confidence each way.
     */
    public OptionalInt winner(int setting, double speed) {
        int value = nativeWinner(open(), setting, speed);
        return value < 0 ? OptionalInt.empty() : OptionalInt.of(value);
    }

    /** Sets the confidence a winner needs; see {@link #winner}. */
    public void setConfidence(double confidence) {
        nativeSetConfidence(open(), confidence);
    }

    /** Frees the native object. Using it afterwards throws {@link IllegalStateException}. */
    @Override
    public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private long open() {
        if (handle == 0) {
            throw new IllegalStateException("the blind trials are closed");
        }
        return handle;
    }

    private static native long nativeCreate(long seed);
    private static native void nativeDestroy(long handle);
    private static native int nativeAddSetting(long handle);
    private static native int nativeAddValue(long handle, int setting, double value);
    private static native void nativeSetAvailable(long handle, int setting, boolean available);
    private static native boolean nativeAdd(long handle, int setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore, int preferred);
    private static native int nativeNext(long handle, double speed);
    private static native double nativeNextFirst(long handle);
    private static native double nativeNextSecond(long handle);
    private static native int nativeWon(long handle, int setting, double speed, int value);
    private static native int nativeLost(long handle, int setting, double speed, int value);
    private static native int nativeTied(long handle, int setting, double speed, int value);
    private static native int nativeHeard(long handle, int setting, double speed, int value);
    private static native double nativeMeanScore(long handle, int setting, double speed, int value);
    private static native int nativeWinner(long handle, int setting, double speed);
    private static native void nativeSetConfidence(long handle, double confidence);
}
