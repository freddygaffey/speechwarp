package io.github.fredgaffey.speechwarp

import kotlin.math.exp
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Runs on this computer's JVM against a native library built for it; see build.gradle.kts. */
class ListenerTrainerTest {
    /** A listener who follows 14 syllables a second half the time; deterministic, no noise. */
    private fun listener(rate: Double) = 1 / (1 + exp((rate - 14.0) / 2.0))

    private fun runTest(trainer: ListenerTrainer, time: Double): Double {
        trainer.testBegin(10.0, time)
        var presented = 0
        while (!trainer.testDone && presented < 60) {
            val rate = trainer.testRate()
            assertTrue(rate in 3.0..60.0)
            assertTrue(trainer.addMeasure(TrainerMeasure.INTELLIGIBILITY, listener(rate), 8.0, rate, time + presented))
            presented++
        }
        return trainer.testEnd(time + presented)
    }

    @Test
    fun enumsHaveTheCValues() {
        assertEquals(listOf(0, 1, 2, 3), TrainerMeasure.entries.map { it.value })
        assertEquals(listOf(0, 1, 2, 3), TrainerPlan.entries.map { it.value })
        assertEquals((0..10).toList(), TrainerParam.entries.map { it.value })
        assertEquals(
            listOf("TARGET", "MARGIN", "RAMP_START", "RAMP_STEP", "RAMP_MINUTES", "INTERVAL_SPREAD", "INTERVAL_MINUTES",
                "TRACKING_GAIN", "RETENTION_COST", "TEST_MAX", "TEST_PRECISION"),
            TrainerParam.entries.map { it.name },
        )
    }

    @Test
    fun weightsAndParametersReadBack() {
        ListenerTrainer().use { trainer ->
            assertEquals(0.5, trainer.getWeight(TrainerMeasure.INTELLIGIBILITY), 0.0)
            assertEquals(1.0, trainer.getWeight(TrainerMeasure.VERIFICATION), 0.0)
            assertEquals(0.3, trainer.getWeight(TrainerMeasure.RATING), 1e-12)
            assertEquals(0.75, trainer.getParam(TrainerParam.TARGET), 0.0)
            assertEquals(40.0, trainer.getParam(TrainerParam.TEST_MAX), 0.0)
            trainer.setWeight(TrainerMeasure.RATING, 0.1)
            trainer.setParam(TrainerParam.MARGIN, 0.2)
            assertEquals(0.1, trainer.getWeight(TrainerMeasure.RATING), 0.0)
            assertEquals(0.2, trainer.getParam(TrainerParam.MARGIN), 0.0)
            assertFalse(trainer.addMeasure(TrainerMeasure.RETENTION, 0.5, 1.0, 10.0, 0.0))
            assertFalse(trainer.testDone)
            assertEquals(0.0, trainer.threshold, 0.0)
        }
    }

    @Test
    fun aThresholdTestFindsAThreshold() {
        ListenerTrainer(7).use { trainer ->
            val threshold = runTest(trainer, 1000.0)
            assertTrue(threshold > 0)
            assertEquals(threshold, trainer.threshold, 0.0)
            assertTrue(trainer.thresholdLow < trainer.threshold)
            assertTrue(trainer.threshold < trainer.thresholdHigh)
            // The listener understands 75% at 14 - 2 ln 3 = 11.8 syllables a second.
            assertEquals(11.8, threshold, 2.0)
        }
    }

    @Test
    fun sessionsAndPlansWithNoData() {
        ListenerTrainer(1).use { trainer ->
            assertEquals(0.0, trainer.sessionRate(0.0), 0.0)
            assertEquals(-1, trainer.sessionEnd(1.0, 10.0))
            for (plan in TrainerPlan.entries) {
                assertEquals(0.0, trainer.planEffect(plan), 0.0)
                assertTrue(trainer.planEffectSd(plan) > 0)
                assertTrue(trainer.planRetention(plan).isNaN())
                assertTrue(trainer.planRetentionSd(plan).isNaN())
                assertEquals(0, trainer.planSessions(plan))
            }
            assertTrue(trainer.trend > 0)
            assertTrue(trainer.trendSd > 0)
            var bestTotal = 0.0
            for (plan in TrainerPlan.values()) {
                val p = trainer.planBestProbability(plan)
                assertTrue(p in 0.0..1.0)
                bestTotal += p
            }
            assertEquals(1.0, bestTotal, 1e-6)

            val threshold = runTest(trainer, 1000.0)
            trainer.sessionBegin(TrainerPlan.STEADY, 5000.0)
            assertEquals(threshold * 1.1, trainer.sessionRate(5000.0), 1e-9 * threshold)
            assertEquals(threshold * 1.1, trainer.sessionRate(5600.0), 1e-9 * threshold)
            assertTrue(trainer.addMeasure(TrainerMeasure.RATING, 0.75, 1.0, threshold * 1.1, 5600.0))
            runTest(trainer, 6000.0)
            val session = trainer.sessionEnd(1.0, 6100.0)
            assertTrue(session >= 0)
            assertTrue(trainer.addRetention(session, 0.8, 5.0, 86400.0, 92000.0))
            assertFalse(trainer.addRetention(session, 2.0, 5.0, 86400.0, 92000.0))
        }
    }

    @Test
    fun sameSeedSamePlans() {
        fun plans(seed: Long) = ListenerTrainer(seed).use { trainer -> List(20) { trainer.nextPlan() } }
        assertEquals(plans(42), plans(42))
        assertTrue(plans(42).toSet().size > 1)
        assertEquals(plans(0), ListenerTrainer().use { trainer -> List(20) { trainer.nextPlan() } })
    }

    @Test
    fun aClosedTrainerThrows() {
        val trainer = ListenerTrainer()
        trainer.close()
        trainer.close()
        assertThrows(IllegalStateException::class.java) { trainer.threshold }
        assertThrows(IllegalStateException::class.java) { trainer.nextPlan() }
    }

    @Test
    fun blindTrialsChooseAndCount() {
        BlindTrials(3).use { trials ->
            assertNull(trials.next(5.0))
            val a = trials.addSetting()
            val b = trials.addSetting()
            assertEquals(0, a)
            assertEquals(1, b)
            assertEquals(0, trials.addValue(a, 0.0))
            assertEquals(1, trials.addValue(a, 0.06))
            assertEquals(-1, trials.addValue(a, 0.06))
            assertEquals(-1, trials.addValue(9, 1.0))
            trials.setAvailable(b, false)

            val first = trials.next(5.5)!!
            assertEquals(a, first.setting)
            assertEquals(setOf(0.0, 0.06), setOf(first.first, first.second))
            assertNull(trials.meanScore(a, 5.5, 0))
            assertNull(trials.winner(a, 5.5))

            // Five trials in the 5 to 6 band, each preferring neither: ties.
            repeat(5) {
                val trial = trials.next(5.5)!!
                assertTrue(trials.add(trial.setting, 5.5, trial.first, trial.second, 0.6, 0.6, 0))
            }
            assertEquals(5, trials.heard(a, 5.9, 0))
            assertEquals(5, trials.heard(a, 5.0, 1))
            assertEquals(5, trials.tied(a, 5.5, 0))
            assertEquals(0, trials.won(a, 5.5, 0))
            assertEquals(0, trials.lost(a, 5.5, 1))
            assertEquals(0.6, trials.meanScore(a, 5.5, 0)!!, 1e-12)
            assertNull(trials.winner(a, 5.5))
            assertEquals(0, trials.heard(a, 7.5, 0)) // another band
            assertFalse(trials.add(a, 5.5, 0.0, 0.5, 0.5, 0.5, 0)) // 0.5 is not a value added
        }
    }

    @Test
    fun blindTrialsNameAWinner() {
        BlindTrials(11).use { trials ->
            val a = trials.addSetting()
            trials.addValue(a, 0.0)
            trials.addValue(a, 1.0)
            trials.setConfidence(0.9)
            repeat(12) { assertTrue(trials.add(a, 6.5, 0.0, 1.0, 0.4, 0.8, 1)) }
            assertEquals(1, trials.winner(a, 6.5))
            assertEquals(12, trials.won(a, 6.5, 1))
            assertEquals(12, trials.lost(a, 6.5, 0))
            assertNotNull(trials.meanScore(a, 6.5, 1))
        }
    }

    @Test
    fun sameSeedSameTrials() {
        fun run(seed: Long) = BlindTrials(seed).use { trials ->
            val a = trials.addSetting()
            trials.addValue(a, 1.0)
            trials.addValue(a, 2.0)
            trials.addValue(a, 3.0)
            List(10) { trials.next(4.5)!!.also { trials.add(a, 4.5, it.first, it.second, 0.5, 0.5, 0) } }
        }
        assertEquals(run(5), run(5))
    }
}
