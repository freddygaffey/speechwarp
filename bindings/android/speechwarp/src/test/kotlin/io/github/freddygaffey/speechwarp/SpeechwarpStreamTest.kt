package io.github.freddygaffey.speechwarp

import java.io.File
import kotlin.math.PI
import kotlin.math.min
import kotlin.math.sin
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Runs on this computer's JVM against a native library built for it; see build.gradle.kts. */
class SpeechwarpStreamTest {
    private val rate = 22050

    /** Something for the library to chew on: a gliding tone in bursts, with gaps. */
    private fun signal(seconds: Double, channels: Int = 1): FloatArray {
        val frames = (rate * seconds).toInt()
        val samples = FloatArray(frames * channels)
        var phase = 0.0
        for (i in 0 until frames) {
            val t = i.toDouble() / rate
            phase += 2 * PI * (120 + 60 * sin(t * 3)) / rate
            val envelope = if (t % 0.4 < 0.3) sin(PI * (t % 0.4) / 0.3) else 0.0
            val value = (0.4 * envelope * (sin(phase) + 0.5 * sin(2 * phase) + 0.3 * sin(3 * phase))).toFloat()
            for (c in 0 until channels) samples[i * channels + c] = value
        }
        return samples
    }

    private fun readAll(stream: SpeechwarpStream): FloatArray {
        val out = FloatArray(stream.framesAvailable * stream.channels)
        assertEquals(out.size / stream.channels, stream.read(out))
        return out
    }

    private fun speedUp(input: FloatArray, speed: Float, nonlinear: Float = 1f): FloatArray =
        SpeechwarpStream(rate).use { stream ->
            stream.speed = speed
            stream.nonlinear = nonlinear
            stream.write(input)
            stream.flush()
            readAll(stream)
        }

    @Test
    fun versionMatchesTheHeader() {
        val header = File(System.getProperty("speechwarp.header")).readText()
        assertTrue(header.contains("#define SPEECHWARP_VERSION \"${SpeechwarpStream.libraryVersion}\""))
    }

    @Test
    fun defaultsAndArguments() {
        SpeechwarpStream(rate, 2).use { stream ->
            assertEquals(1f, stream.speed)
            assertEquals(1f, stream.nonlinear)
            assertEquals(0, stream.framesAvailable)
            assertEquals(0L, stream.position)

            assertThrows(IllegalArgumentException::class.java) { stream.speed = 0f }
            assertThrows(IllegalArgumentException::class.java) { stream.speed = Float.NaN }
            assertThrows(IllegalArgumentException::class.java) { stream.nonlinear = Float.NaN }
            assertThrows(IllegalArgumentException::class.java) { stream.write(FloatArray(3)) }
            assertThrows(IndexOutOfBoundsException::class.java) { stream.write(FloatArray(4), 2, 4) }
            assertThrows(IndexOutOfBoundsException::class.java) { stream.read(ShortArray(4), -1, 2) }

            stream.speed = 1000f
            assertEquals(SpeechwarpStream.MAX_SPEED, stream.speed)
            stream.nonlinear = -3f
            assertEquals(0f, stream.nonlinear)
        }
        assertThrows(IllegalArgumentException::class.java) { SpeechwarpStream(100) }
        assertThrows(IllegalArgumentException::class.java) { SpeechwarpStream(rate, 0) }
    }

    @Test
    fun outputIsShorterByTheSpeed() {
        val input = signal(20.0)
        for ((speed, nonlinear) in listOf(1f to 1f, 3f to 1f, 3f to 0f, 8f to 1f)) {
            val output = speedUp(input, speed, nonlinear)
            assertEquals(speed, input.size.toFloat() / output.size, 0.1f * speed)
        }
    }

    @Test
    fun stereoShortsAndOffsets() {
        val input = signal(5.0, channels = 2)
        val shorts = ShortArray(input.size + 4) { i -> if (i < 4) 12345 else (input[i - 4] * 32767).toInt().toShort() }
        SpeechwarpStream(rate, 2).use { stream ->
            stream.speed = 2f
            stream.write(shorts, 4)
            stream.flush()

            val buffer = ShortArray(10 + 1000 * 2 + 1) // one sample too many: only whole frames are written
            var total = 0
            while (true) {
                buffer[9] = 777
                val frames = stream.read(buffer, 10)
                if (frames == 0) break
                assertTrue(frames <= 1000)
                assertEquals(777.toShort(), buffer[9])
                for (i in 0 until frames) assertEquals(buffer[10 + 2 * i], buffer[10 + 2 * i + 1])
                total += frames
            }
            assertEquals(2.0, (input.size / 2).toDouble() / total, 0.2)
        }
    }

    @Test
    fun positionFollowsWhatIsRead() {
        val input = signal(20.0)
        SpeechwarpStream(rate).use { stream ->
            stream.speed = 4f
            val buffer = FloatArray(512)
            var written = 0
            var last = 0L
            while (true) {
                if (written < input.size && stream.framesAvailable < 512) {
                    val n = min(2000, input.size - written)
                    stream.write(input, written, n)
                    written += n
                    if (written == input.size) stream.flush()
                    continue
                }
                val position = stream.position
                assertTrue(position >= last && position <= written)
                last = position
                if (stream.read(buffer) == 0) break
            }
            assertEquals(input.size.toLong(), stream.position)
        }
    }

    @Test
    fun resetDiscardsEverything() {
        val input = signal(5.0)
        SpeechwarpStream(rate).use { stream ->
            stream.speed = 3f
            stream.write(input)
            assertTrue(stream.framesAvailable > 0)
            stream.reset()
            assertEquals(0, stream.framesAvailable)
            assertEquals(0L, stream.position)
            assertEquals(3f, stream.speed)
            stream.write(input)
            stream.flush()
            assertArrayEquals(speedUp(input, 3f), readAll(stream), 0f)
        }
    }

    @Test
    fun aClosedStreamThrows() {
        val stream = SpeechwarpStream(rate)
        stream.close()
        stream.close()
        assertThrows(IllegalStateException::class.java) { stream.speed = 2f }
        assertThrows(IllegalStateException::class.java) { stream.write(FloatArray(10)) }
    }
}
