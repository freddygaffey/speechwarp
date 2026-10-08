package io.github.freddygaffey.speechwarp

/**
 * Speeds up speech. Write audio in, read the faster audio out.
 *
 * Samples are interleaved: a frame is one sample per channel. Floats are in the range -1 to 1. Offsets and
 * lengths of arrays are in samples, as usual for arrays; what [read] returns, [framesAvailable] and
 * [position] are in frames.
 *
 * A stream is not thread safe: use it from one thread, or lock around it. [close] it when done; the garbage
 * collector will free a forgotten one eventually, but not promptly.
 *
 * @param sampleRate samples per second, 4000 to 384000
 * @param channels 1 to 32
 * @throws IllegalArgumentException if the sample rate or channel count is not supported
 */
class SpeechwarpStream(val sampleRate: Int, val channels: Int = 1) : AutoCloseable {
    private var handle: Long

    init {
        require(sampleRate in 4000..384000) { "sampleRate must be from 4000 to 384000, not $sampleRate" }
        require(channels in 1..32) { "channels must be from 1 to 32, not $channels" }
        handle = nativeCreate(sampleRate, channels)
        if (handle == 0L) throw OutOfMemoryError("speechwarp could not allocate a stream")
    }

    /**
     * Overall speed: 2 plays twice as fast. Clamped to [MIN_SPEED]..[MAX_SPEED].
     *
     * Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
     * speed-up the speed varies from moment to moment and its average is steered to this value; expect the
     * result within a few percent.
     */
    var speed: Float
        get() = nativeGetSpeed(open())
        set(value) {
            require(value > 0) { "speed must be greater than zero, not $value" }
            nativeSetSpeed(open(), value)
        }

    /**
     * How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
     * pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
     */
    var nonlinear: Float
        get() = nativeGetNonlinear(open())
        set(value) {
            require(!value.isNaN()) { "nonlinear must be a number" }
            nativeSetNonlinear(open(), value)
        }

    // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped. See
    // docs/how-it-works.md.

    /**
     * Shortens every pause to at most this many seconds of input before speeding up, so that the speed is spent
     * on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. [position] counts the
     * frames left out.
     */
    var pauseCap: Float
        get() = nativeGetPauseCap(open())
        set(value) {
            require(!value.isNaN()) { "pauseCap must be a number" }
            nativeSetPauseCap(open(), value)
        }

    /**
     * While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses is
     * spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
     * When false, trimmed pauses make playback faster than [speed].
     */
    var keepSpeed: Boolean
        get() = nativeGetKeepSpeed(open())
        set(value) = nativeSetKeepSpeed(open(), value)

    /**
     * No 10 ms block of speech plays slower than this fraction of [speed]: at 8x, 0.5 keeps every block at 4x or
     * more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with nonlinear
     * speed-up above 1x.
     */
    var speedFloor: Float
        get() = nativeGetSpeedFloor(open())
        set(value) {
            require(!value.isNaN()) { "speedFloor must be a number" }
            nativeSetSpeedFloor(open(), value)
        }

    /**
     * Seconds of silence put into the output [rhythmRate] times a second, at the quietest point nearby, which can
     * help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06. Allowed: 0,
     * or 0.005 to 0.2. [position] holds still during a gap.
     */
    var rhythmGap: Float
        get() = nativeGetRhythmGap(open())
        set(value) {
            require(!value.isNaN()) { "rhythmGap must be a number" }
            nativeSetRhythmGap(open(), value)
        }

    /** Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. */
    var rhythmRate: Float
        get() = nativeGetRhythmRate(open())
        set(value) {
            require(value > 0) { "rhythmRate must be greater than zero, not $value" }
            nativeSetRhythmRate(open(), value)
        }

    // Options that follow the speed. Off by default; they set the pause cap and speed floor again whenever the
    // speed changes. Setting the fixed option turns the rule off. NaN is rejected here; the C library ignores it.

    /**
     * Heard pause: keeps each pause about [seconds] long in the output, by setting the pause cap to [seconds]
     * times the current speed (clamped to 0.03 to 0.4 s of input) whenever the speed changes. Below [fromSpeed]
     * pauses are left alone. [seconds]: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to
     * 0.4. [fromSpeed]: 1 to 20. Sensible: 0.015 to 0.06 heard, from 3x. Setting [pauseCap] turns the rule off
     * (and [heardPause] reads 0); [pauseCap] reads the value in force.
     */
    fun setHeardPause(seconds: Float, fromSpeed: Float) {
        require(!seconds.isNaN()) { "seconds must be a number" }
        require(!fromSpeed.isNaN()) { "fromSpeed must be a number" }
        nativeSetHeardPause(open(), seconds, fromSpeed)
    }

    /** The heard pause last set with [setHeardPause], in seconds; 0 if the rule is off. */
    val heardPause: Float
        get() = nativeGetHeardPause(open())

    /** The speed from which [setHeardPause] applies, as last set. */
    val heardPauseFrom: Float
        get() = nativeGetHeardPauseFrom(open())

    /**
     * Floor blend: the speed floor in force is 0 below [fromSpeed], rises linearly to [fraction] at [fullSpeed],
     * and stays at [fraction] above it, so that a speed ramp never changes the sound in a jump. [fraction]: 0
     * turns the rule off (and the floor with it); otherwise up to 1. Speeds 1 to 20; if [fullSpeed] is not above
     * [fromSpeed] it is taken as equal, and the floor steps to [fraction] at [fromSpeed]. Sensible: 0.5 from 4x,
     * full at 6x. Setting [speedFloor] turns the rule off
     * (and [floorBlend] reads 0); [speedFloor] reads the value in force.
     */
    fun setFloorBlend(fraction: Float, fromSpeed: Float, fullSpeed: Float) {
        require(!fraction.isNaN()) { "fraction must be a number" }
        require(!fromSpeed.isNaN()) { "fromSpeed must be a number" }
        require(!fullSpeed.isNaN()) { "fullSpeed must be a number" }
        nativeSetFloorBlend(open(), fraction, fromSpeed, fullSpeed)
    }

    /** The floor fraction last set with [setFloorBlend]; 0 if the rule is off. */
    val floorBlend: Float
        get() = nativeGetFloorBlend(open())

    /** The speed from which the floor blend starts, as last set. */
    val floorBlendFrom: Float
        get() = nativeGetFloorBlendFrom(open())

    /** The speed at which the floor blend reaches its full fraction, as last set. */
    val floorBlendFull: Float
        get() = nativeGetFloorBlendFull(open())

    /**
     * Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
     * been written since creation or [reset]. Multiply by the speed for the rate heard. An estimate, typically
     * within about 10%.
     */
    val syllableRate: Double?
        get() = nativeSyllableRate(open()).takeIf { it >= 0 }

    /** Frames of output ready to read. */
    val framesAvailable: Int
        get() = nativeAvailable(open())

    /**
     * The input frame, counted from creation or the last [reset], that the next output frame to be read was
     * made from. This is how a player maps what is being heard back to a place in the source.
     *
     * It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
     * [flush] has been read it equals the number of frames written.
     */
    val position: Long
        get() = nativePosition(open())

    /**
     * Adds input. The output does not depend on how the input is divided between calls.
     *
     * @param length the number of samples, which must be a whole number of frames
     */
    @JvmOverloads
    fun write(samples: FloatArray, offset: Int = 0, length: Int = samples.size - offset) {
        checkRange(samples.size, offset, length)
        require(length % channels == 0) { "$length samples is not a whole number of $channels-channel frames" }
        if (!nativeWriteFloat(open(), samples, offset, length / channels)) throw OutOfMemoryError()
    }

    /** @see write */
    @JvmOverloads
    fun write(samples: ShortArray, offset: Int = 0, length: Int = samples.size - offset) {
        checkRange(samples.size, offset, length)
        require(length % channels == 0) { "$length samples is not a whole number of $channels-channel frames" }
        if (!nativeWriteShort(open(), samples, offset, length / channels)) throw OutOfMemoryError()
    }

    /**
     * Takes processed output, as many whole frames as fit in [length] samples.
     *
     * @return the number of frames written, which is the number of samples divided by [channels]. It may be
     * 0: output lags input by a short look-ahead.
     */
    @JvmOverloads
    fun read(samples: FloatArray, offset: Int = 0, length: Int = samples.size - offset): Int {
        checkRange(samples.size, offset, length)
        return nativeReadFloat(open(), samples, offset, length / channels)
    }

    /** @see read */
    @JvmOverloads
    fun read(samples: ShortArray, offset: Int = 0, length: Int = samples.size - offset): Int {
        checkRange(samples.size, offset, length)
        return nativeReadShort(open(), samples, offset, length / channels)
    }

    /**
     * Processes everything written so far, at the end of the input. Read until empty afterwards. Writing more
     * starts a new stretch of audio, and [position] carries on counting.
     */
    fun flush() {
        if (!nativeFlush(open())) throw OutOfMemoryError()
    }

    /**
     * Discards all buffered input and output, keeping the speed and nonlinear settings, and starts [position]
     * again from zero. Use after seeking.
     */
    fun reset() = nativeReset(open())

    /** Frees the native stream. Using the stream afterwards throws [IllegalStateException]. */
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0
        }
    }

    protected fun finalize() = close()

    private fun open(): Long {
        check(handle != 0L) { "the stream is closed" }
        return handle
    }

    private fun checkRange(size: Int, offset: Int, length: Int) {
        if (offset < 0 || length < 0 || offset > size - length) {
            throw IndexOutOfBoundsException("offset $offset and length $length do not fit an array of $size")
        }
    }

    companion object {
        /** The slowest speed that can be set. */
        const val MIN_SPEED = 0.05f

        /** The fastest speed that can be set. */
        const val MAX_SPEED = 20f

        init {
            System.loadLibrary("speechwarp_jni")
        }

        /** The version of the native library, such as "0.3.0". */
        @JvmStatic
        val libraryVersion: String
            get() = nativeVersion()

        @JvmStatic private external fun nativeVersion(): String
        @JvmStatic private external fun nativeCreate(sampleRate: Int, channels: Int): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeSetSpeed(handle: Long, speed: Float)
        @JvmStatic private external fun nativeGetSpeed(handle: Long): Float
        @JvmStatic private external fun nativeSetNonlinear(handle: Long, amount: Float)
        @JvmStatic private external fun nativeGetNonlinear(handle: Long): Float
        @JvmStatic private external fun nativeSetPauseCap(handle: Long, value: Float)
        @JvmStatic private external fun nativeGetPauseCap(handle: Long): Float
        @JvmStatic private external fun nativeSetKeepSpeed(handle: Long, enabled: Boolean)
        @JvmStatic private external fun nativeGetKeepSpeed(handle: Long): Boolean
        @JvmStatic private external fun nativeSetSpeedFloor(handle: Long, value: Float)
        @JvmStatic private external fun nativeGetSpeedFloor(handle: Long): Float
        @JvmStatic private external fun nativeSetRhythmGap(handle: Long, value: Float)
        @JvmStatic private external fun nativeGetRhythmGap(handle: Long): Float
        @JvmStatic private external fun nativeSetRhythmRate(handle: Long, value: Float)
        @JvmStatic private external fun nativeGetRhythmRate(handle: Long): Float
        @JvmStatic private external fun nativeSetHeardPause(handle: Long, seconds: Float, fromSpeed: Float)
        @JvmStatic private external fun nativeGetHeardPause(handle: Long): Float
        @JvmStatic private external fun nativeGetHeardPauseFrom(handle: Long): Float
        @JvmStatic private external fun nativeSetFloorBlend(handle: Long, fraction: Float, fromSpeed: Float, fullSpeed: Float)
        @JvmStatic private external fun nativeGetFloorBlend(handle: Long): Float
        @JvmStatic private external fun nativeGetFloorBlendFrom(handle: Long): Float
        @JvmStatic private external fun nativeGetFloorBlendFull(handle: Long): Float
        @JvmStatic private external fun nativeSyllableRate(handle: Long): Double
        @JvmStatic private external fun nativeAvailable(handle: Long): Int
        @JvmStatic private external fun nativePosition(handle: Long): Long
        @JvmStatic private external fun nativeFlush(handle: Long): Boolean
        @JvmStatic private external fun nativeReset(handle: Long)
        @JvmStatic private external fun nativeWriteFloat(handle: Long, samples: FloatArray, offset: Int, frames: Int): Boolean
        @JvmStatic private external fun nativeWriteShort(handle: Long, samples: ShortArray, offset: Int, frames: Int): Boolean
        @JvmStatic private external fun nativeReadFloat(handle: Long, samples: FloatArray, offset: Int, maxFrames: Int): Int
        @JvmStatic private external fun nativeReadShort(handle: Long, samples: ShortArray, offset: Int, maxFrames: Int): Int
    }
}
