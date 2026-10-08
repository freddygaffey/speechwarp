package io.github.freddygaffey.speechwarp

/** One blind A/B trial to run: the [setting] to compare and its two values in the order to play them. */
data class BlindTrial(val setting: Int, val first: Double, val second: Double)

/**
 * Designs the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
 * values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
 * caller names the settings; here they are numbers (0, 1, ... in the order added). Deterministic for a given
 * seed.
 *
 * Not thread safe. [close] it when done.
 *
 * @param seed the seed for the random choices, as the bit pattern of an unsigned 64-bit number
 */
class BlindTrials(seed: Long = 0) : AutoCloseable {
    private var handle: Long

    init {
        handle = nativeCreate(seed)
        if (handle == 0L) throw OutOfMemoryError("speechwarp could not allocate blind trials")
    }

    /** Adds a setting. @return its number (0, 1, ...), or -1 */
    fun addSetting(): Int = nativeAddSetting(open())

    /** Adds a [value] to compare to a [setting]. @return its number within the setting, or -1 (duplicate, bad setting) */
    fun addValue(setting: Int, value: Double): Int = nativeAddValue(open(), setting, value)

    /** Leaves a setting out of [next] while false (say, when its method is not available). Default true. */
    fun setAvailable(setting: Int, available: Boolean) = nativeSetAvailable(open(), setting, available)

    /**
     * Records a trial at [speed]: the two values in the order heard, each one's score 0..1, and [preferred]: -1
     * the first, 1 the second, 0 neither. Values must be ones added.
     *
     * @return false if an argument is invalid
     */
    fun add(
        setting: Int,
        speed: Double,
        firstValue: Double,
        secondValue: Double,
        firstScore: Double,
        secondScore: Double,
        preferred: Int,
    ): Boolean = nativeAdd(open(), setting, speed, firstValue, secondValue, firstScore, secondScore, preferred)

    /**
     * Chooses the next trial at [speed]: the available setting with the fewest trials in its band (ties at
     * random), its pair of values compared least (ties at random), in random order. Null if no setting has two
     * values.
     */
    fun next(speed: Double): BlindTrial? {
        val trials = open()
        val setting = nativeNext(trials, speed)
        return if (setting < 0) null else BlindTrial(setting, nativeNextFirst(trials), nativeNextSecond(trials))
    }

    /** Comparisons a value has won in the band of [speed]; [value] is its number within the setting. */
    fun won(setting: Int, speed: Double, value: Int): Int = nativeWon(open(), setting, speed, value)

    /** Comparisons a value has lost in the band of [speed]. */
    fun lost(setting: Int, speed: Double, value: Int): Int = nativeLost(open(), setting, speed, value)

    /** Comparisons a value has tied in the band of [speed]. */
    fun tied(setting: Int, speed: Double, value: Int): Int = nativeTied(open(), setting, speed, value)

    /** Trials a value was heard in, in the band of [speed]. */
    fun heard(setting: Int, speed: Double, value: Int): Int = nativeHeard(open(), setting, speed, value)

    /** A value's mean score in the band of [speed]; null if never heard. */
    fun meanScore(setting: Int, speed: Double, value: Int): Double? =
        nativeMeanScore(open(), setting, speed, value).takeIf { !it.isNaN() }

    /**
     * The value (its number within the setting) with a reliable win in the band of [speed], or null. A value wins
     * when it has been heard in at least 5 trials, has met every other value in at least 3, and against each the
     * Bayes factor for "preferred" over "no preference" is at least 1 / (1 - confidence): 20 at the default 0.95.
     * However often this is asked, the chance of ever naming a winner between two values that are really alike is
     * at most 1 - confidence each way.
     */
    fun winner(setting: Int, speed: Double): Int? = nativeWinner(open(), setting, speed).takeIf { it >= 0 }

    /** Sets the confidence a winner needs; see [winner]. */
    fun setConfidence(confidence: Double) = nativeSetConfidence(open(), confidence)

    /** Frees the native object. Using it afterwards throws [IllegalStateException]. */
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0
        }
    }

    protected fun finalize() = close()

    private fun open(): Long {
        check(handle != 0L) { "the blind trials are closed" }
        return handle
    }

    private companion object {
        init {
            System.loadLibrary("speechwarp_jni")
        }

        @JvmStatic private external fun nativeCreate(seed: Long): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeAddSetting(handle: Long): Int
        @JvmStatic private external fun nativeAddValue(handle: Long, setting: Int, value: Double): Int
        @JvmStatic private external fun nativeSetAvailable(handle: Long, setting: Int, available: Boolean)
        @JvmStatic private external fun nativeAdd(handle: Long, setting: Int, speed: Double, firstValue: Double, secondValue: Double, firstScore: Double, secondScore: Double, preferred: Int): Boolean
        @JvmStatic private external fun nativeNext(handle: Long, speed: Double): Int
        @JvmStatic private external fun nativeNextFirst(handle: Long): Double
        @JvmStatic private external fun nativeNextSecond(handle: Long): Double
        @JvmStatic private external fun nativeWon(handle: Long, setting: Int, speed: Double, value: Int): Int
        @JvmStatic private external fun nativeLost(handle: Long, setting: Int, speed: Double, value: Int): Int
        @JvmStatic private external fun nativeTied(handle: Long, setting: Int, speed: Double, value: Int): Int
        @JvmStatic private external fun nativeHeard(handle: Long, setting: Int, speed: Double, value: Int): Int
        @JvmStatic private external fun nativeMeanScore(handle: Long, setting: Int, speed: Double, value: Int): Double
        @JvmStatic private external fun nativeWinner(handle: Long, setting: Int, speed: Double): Int
        @JvmStatic private external fun nativeSetConfidence(handle: Long, confidence: Double)
    }
}
