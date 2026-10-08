package io.github.fredgaffey.speechwarp

import java.io.File
import kotlin.math.PI
import kotlin.math.min
import kotlin.math.sin
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
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

    @Test
    fun highSpeedOptionsStartOffAndClamp() {
        SpeechwarpStream(rate).use { stream ->
            assertEquals(0f, stream.pauseCap)
            assertTrue(stream.keepSpeed)
            assertEquals(0f, stream.speedFloor)
            assertEquals(0f, stream.rhythmGap)
            assertEquals(5f, stream.rhythmRate)
            assertNull(stream.syllableRate)
            stream.pauseCap = 5f
            stream.speedFloor = 0.5f
            stream.rhythmGap = 0.04f
            stream.rhythmRate = 100f
            stream.keepSpeed = false
            assertEquals(1f, stream.pauseCap)
            assertEquals(0.5f, stream.speedFloor)
            assertEquals(0.04f, stream.rhythmGap, 1e-6f)
            assertEquals(16f, stream.rhythmRate)
            assertFalse(stream.keepSpeed)
            assertThrows(IllegalArgumentException::class.java) { stream.pauseCap = Float.NaN }
            assertThrows(IllegalArgumentException::class.java) { stream.rhythmRate = 0f }
        }
    }

    @Test
    fun pauseCapShortensAndPositionReachesTheEnd() {
        val input = signal(12.0)
        SpeechwarpStream(rate).use { stream ->
            stream.nonlinear = 0f
            stream.pauseCap = 0.03f
            stream.keepSpeed = false
            stream.write(input)
            stream.flush()
            assertTrue(readAll(stream).size < input.size * 0.9)
            assertEquals(input.size.toLong(), stream.position)
            assertNotNull(stream.syllableRate)
        }
    }

    @Test
    fun heardPauseAndFloorBlendFollowTheSpeed() {
        SpeechwarpStream(rate).use { stream ->
            assertEquals(0f, stream.heardPause)
            assertEquals(0f, stream.floorBlend)

            stream.setHeardPause(0.03f, 3f)
            assertEquals(0.03f, stream.heardPause, 1e-6f)
            assertEquals(3f, stream.heardPauseFrom)
            stream.speed = 2f
            assertEquals(0f, stream.pauseCap)
            stream.speed = 5f
            assertEquals(0.15f, stream.pauseCap, 1e-6f)
            stream.speed = 20f
            assertEquals(0.4f, stream.pauseCap, 1e-6f) // clamped to 0.4 s of input

            stream.setFloorBlend(0.5f, 4f, 6f)
            assertEquals(0.5f, stream.floorBlend)
            assertEquals(4f, stream.floorBlendFrom)
            assertEquals(6f, stream.floorBlendFull)
            stream.speed = 3f
            assertEquals(0f, stream.speedFloor)
            stream.speed = 5f
            assertEquals(0.25f, stream.speedFloor, 1e-6f)
            stream.speed = 8f
            assertEquals(0.5f, stream.speedFloor, 1e-6f)

            // Setting the fixed options turns the rules off: the speed no longer moves them.
            stream.pauseCap = 0.1f
            stream.speedFloor = 0.2f
            stream.speed = 6f
            assertEquals(0.1f, stream.pauseCap, 1e-6f)
            assertEquals(0.2f, stream.speedFloor, 1e-6f)
            assertEquals(0f, stream.heardPause)
            assertEquals(0f, stream.floorBlend)

            stream.setHeardPause(0f, 3f)
            assertEquals(0f, stream.heardPause)
            assertThrows(IllegalArgumentException::class.java) { stream.setHeardPause(Float.NaN, 3f) }
            assertThrows(IllegalArgumentException::class.java) { stream.setFloorBlend(0.5f, Float.NaN, 6f) }
        }
    }

    @Test
    fun syllableCounterMatchesTheStream() {
        val input = signal(25.0)
        SpeechwarpStream(rate).use { stream ->
            SyllableCounter(rate).use { counter ->
                assertNull(counter.rate())
                val head = rate * 5
                stream.write(input, 0, head)
                counter.write(input, 0, head)
                assertNull(stream.syllableRate)
                assertNull(counter.rate())

                stream.write(input, head, input.size - head)
                counter.write(input, head, input.size - head)
                val expected = stream.syllableRate!!
                assertTrue(expected > 0)
                assertEquals(expected, counter.rate()!!, 0.0)
                // A minimum longer than what was written gives nothing; a short one gives a number.
                assertNull(counter.rate(120.0, 40.0)) // 25 s written, 40 s needed
                assertNotNull(counter.rate(60.0, 0.0))

                counter.reset()
                assertNull(counter.rate())
            }
        }
    }

    @Test
    fun syllableCounterTakesShortsAndChecksArguments() {
        val input = signal(12.0, channels = 2)
        val shorts = ShortArray(input.size) { (input[it] * 32767).toInt().toShort() }
        SpeechwarpStream(rate, 2).use { stream ->
            SyllableCounter(rate, 2).use { counter ->
                stream.write(shorts)
                counter.write(shorts)
                assertEquals(stream.syllableRate!!, counter.rate()!!, 0.0)
                assertThrows(IllegalArgumentException::class.java) { counter.write(FloatArray(3)) }
                assertThrows(IndexOutOfBoundsException::class.java) { counter.write(shorts, 2, shorts.size) }
            }
        }
        assertThrows(IllegalArgumentException::class.java) { SyllableCounter(100) }
        assertThrows(IllegalArgumentException::class.java) { SyllableCounter(rate, 33) }
        val counter = SyllableCounter(rate)
        counter.close()
        counter.close()
        assertThrows(IllegalStateException::class.java) { counter.rate() }
    }
}
