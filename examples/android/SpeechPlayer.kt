// Playing 16-bit PCM through speechwarp to an AudioTrack. This is a sketch of the playback thread of a
// player, not a whole app: decoding (MediaCodec, ExoPlayer, ...) is up to you and is behind `Source` here.
// It compiles against the library; it has not been run on a device.
package com.example.speechplayer

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import io.github.freddygaffey.speechwarp.SpeechwarpStream
import kotlin.concurrent.thread

/** Where decoded audio comes from: interleaved 16-bit samples. */
interface Source {
    val sampleRate: Int
    val channels: Int

    /** Fills [into] from the current place and returns the number of samples, or 0 at the end. */
    fun read(into: ShortArray): Int

    fun seekTo(frame: Long)
}

class SpeechPlayer(private val source: Source) {
    private val stream = SpeechwarpStream(source.sampleRate, source.channels)
    private val lock = Any() // the stream is not thread safe: the UI thread sets the speed and seeks
    private var base = 0L    // the frame of the source at which the stream was last reset
    @Volatile private var running = false

    var speed: Float
        get() = synchronized(lock) { stream.speed }
        set(value) = synchronized(lock) { stream.speed = value } // heard within a fraction of a second

    /** False for plain, even speed-up. */
    var nonlinear: Boolean
        get() = synchronized(lock) { stream.nonlinear > 0 }
        set(value) = synchronized(lock) { stream.nonlinear = if (value) 1f else 0f }

    /** The frame of the source being heard, for the progress bar. */
    val position: Long
        get() = synchronized(lock) { base + stream.position }

    fun seekTo(frame: Long) = synchronized(lock) {
        source.seekTo(frame)
        stream.reset() // drop what was buffered for the old place; the settings are kept
        base = frame
    }

    fun start() {
        val format = AudioFormat.Builder()
            .setSampleRate(source.sampleRate)
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setChannelMask(if (source.channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO)
            .build()
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder().setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build()
            )
            .setAudioFormat(format)
            .setBufferSizeInBytes(AudioTrack.getMinBufferSize(source.sampleRate, format.channelMask, format.encoding))
            .build()

        running = true
        thread(name = "speechwarp-playback") {
            val input = ShortArray(4096 * source.channels)
            val output = ShortArray(2048 * source.channels)
            var ended = false
            track.play()
            while (running) {
                val frames = synchronized(lock) {
                    // Feed until a block is ready. At speed s a block of output takes about s blocks of input.
                    while (stream.framesAvailable < output.size / source.channels && !ended) {
                        val samples = source.read(input)
                        if (samples == 0) {
                            stream.flush() // no more input: let out what is held back
                            ended = true
                        } else {
                            stream.write(input, 0, samples)
                        }
                    }
                    stream.read(output) // frames, not samples
                }
                if (frames == 0) break // the end
                track.write(output, 0, frames * source.channels) // blocks until the device has room
            }
            track.stop()
            track.release()
        }
    }

    fun stop() {
        running = false
    }

    fun release() = synchronized(lock) { stream.close() }
}
