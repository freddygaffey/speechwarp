package io.github.freddygaffey.speechwarp

/**
 * The syllable-rate estimator behind [SpeechwarpStream.syllableRate], on its own, for audio that does not go
 * through a stream (a player using some other speed-up, or measuring a file). A counter given the same input as
 * a stream reports the same rate as the stream does with a window of 60 s and a minimum of 10 s. Float input is
 * converted to 16 bits exactly as the stream converts it.
 *
 * Samples are interleaved: a frame is one sample per channel. Offsets and lengths of arrays are in samples.
 *
 * A counter is not thread safe: use it from one thread, or lock around it. [close] it when done; the garbage
 * collector will free a forgotten one eventually, but not promptly.
 *
 * @param sampleRate samples per second, 4000 to 384000
 * @param channels 1 to 32
 * @throws IllegalArgumentException if the sample rate or channel count is not supported
 */
class SyllableCounter(val sampleRate: Int, val channels: Int = 1) : AutoCloseable {
    private var handle: Long

    init {
        require(sampleRate in 4000..384000) { "sampleRate must be from 4000 to 384000, not $sampleRate" }
        require(channels in 1..32) { "channels must be from 1 to 32, not $channels" }
        handle = nativeCreate(sampleRate, channels)
        if (handle == 0L) throw OutOfMemoryError("speechwarp could not allocate a syllable counter")
    }

    /**
     * Adds input.
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
     * Syllables a second over the last [windowSeconds] written (or all of it, if less), or null until
     * [minimumSeconds] have been written. The window is clamped to 1 to 120 s, the minimum to 0 up to the window.
     * Multiply by the speed for the rate heard.
     */
    @JvmOverloads
    fun rate(windowSeconds: Double = 60.0, minimumSeconds: Double = 10.0): Double? =
        nativeRate(open(), windowSeconds, minimumSeconds).takeIf { it >= 0 }

    /** Forgets everything written. */
    fun reset() = nativeReset(open())

    /** Frees the native counter. Using it afterwards throws [IllegalStateException]. */
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0
        }
    }

    protected fun finalize() = close()

    private fun open(): Long {
        check(handle != 0L) { "the syllable counter is closed" }
        return handle
    }

    private fun checkRange(size: Int, offset: Int, length: Int) {
        if (offset < 0 || length < 0 || offset > size - length) {
            throw IndexOutOfBoundsException("offset $offset and length $length do not fit an array of $size")
        }
    }

    private companion object {
        init {
            System.loadLibrary("speechwarp_jni")
        }

        @JvmStatic private external fun nativeCreate(sampleRate: Int, channels: Int): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeWriteFloat(handle: Long, samples: FloatArray, offset: Int, frames: Int): Boolean
        @JvmStatic private external fun nativeWriteShort(handle: Long, samples: ShortArray, offset: Int, frames: Int): Boolean
        @JvmStatic private external fun nativeRate(handle: Long, windowSeconds: Double, minimumSeconds: Double): Double
        @JvmStatic private external fun nativeReset(handle: Long)
    }
}
