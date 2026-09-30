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

        /** The version of the native library, such as "0.1.0". */
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
