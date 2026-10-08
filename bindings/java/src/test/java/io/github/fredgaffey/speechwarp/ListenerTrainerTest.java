package io.github.fredgaffey.speechwarp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import org.junit.Test;

public class ListenerTrainerTest {
    /** A listener who follows 14 syllables a second half the time; deterministic, no noise. */
    private static double listener(double rate) {
        return 1 / (1 + Math.exp((rate - 14.0) / 2.0));
    }

    private static double runTest(ListenerTrainer trainer, double time) {
        trainer.testBegin(10, time);
        int presented = 0;
        while (!trainer.testDone() && presented < 60) {
            double rate = trainer.testRate();
            assertTrue(rate >= 3 && rate <= 60);
            assertTrue(trainer.addMeasure(TrainerMeasure.INTELLIGIBILITY, listener(rate), 8, rate, time + presented));
            presented++;
        }
        return trainer.testEnd(time + presented);
    }

    @Test
    public void enumsHaveTheCValues() {
        int i = 0;
        for (TrainerMeasure measure : TrainerMeasure.values()) {
            assertEquals(i++, measure.value());
        }
        assertEquals(4, i);
        i = 0;
        for (TrainerPlan plan : TrainerPlan.values()) {
            assertEquals(i++, plan.value());
        }
        assertEquals(4, i);
        i = 0;
        for (TrainerParam param : TrainerParam.values()) {
            assertEquals(i++, param.value());
        }
        assertEquals(11, i);
        assertEquals("TEST_PRECISION", TrainerParam.values()[10].name());
    }

    @Test
    public void weightsAndParametersReadBack() {
        try (ListenerTrainer trainer = new ListenerTrainer()) {
            assertEquals(0.5, trainer.getWeight(TrainerMeasure.INTELLIGIBILITY), 0);
            assertEquals(1.0, trainer.getWeight(TrainerMeasure.VERIFICATION), 0);
            assertEquals(0.3, trainer.getWeight(TrainerMeasure.RATING), 1e-12);
            assertEquals(0.75, trainer.getParam(TrainerParam.TARGET), 0);
            assertEquals(40.0, trainer.getParam(TrainerParam.TEST_MAX), 0);
            trainer.setWeight(TrainerMeasure.RATING, 0.1);
            trainer.setParam(TrainerParam.MARGIN, 0.2);
            assertEquals(0.1, trainer.getWeight(TrainerMeasure.RATING), 0);
            assertEquals(0.2, trainer.getParam(TrainerParam.MARGIN), 0);
            assertFalse(trainer.addMeasure(TrainerMeasure.RETENTION, 0.5, 1, 10, 0));
            assertFalse(trainer.testDone());
            assertEquals(0.0, trainer.threshold(), 0);
        }
    }

    @Test
    public void aThresholdTestFindsAThreshold() {
        try (ListenerTrainer trainer = new ListenerTrainer(7)) {
            double threshold = runTest(trainer, 1000);
            assertTrue(threshold > 0);
            assertEquals(threshold, trainer.threshold(), 0);
            assertTrue(trainer.thresholdLow() < trainer.threshold());
            assertTrue(trainer.threshold() < trainer.thresholdHigh());
            // The listener understands 75% at 14 - 2 ln 3 = 11.8 syllables a second.
            assertEquals(11.8, threshold, 2.0);
        }
    }

    @Test
    public void sessionsAndPlansWithNoData() {
        try (ListenerTrainer trainer = new ListenerTrainer(1)) {
            assertEquals(0.0, trainer.sessionRate(0), 0);
            assertEquals(-1, trainer.sessionEnd(1, 10));
            for (TrainerPlan plan : TrainerPlan.values()) {
                assertEquals(0.0, trainer.planEffect(plan), 0);
                assertTrue(trainer.planEffectSd(plan) > 0);
                assertTrue(Double.isNaN(trainer.planRetention(plan)));
                assertTrue(Double.isNaN(trainer.planRetentionSd(plan)));
                assertEquals(0, trainer.planSessions(plan));
            }
            assertTrue(trainer.trend() > 0);
            assertTrue(trainer.trendSd() > 0);
            double bestTotal = 0;
            for (TrainerPlan plan : TrainerPlan.values()) {
                double p = trainer.planBestProbability(plan);
                assertTrue(p >= 0 && p <= 1);
                bestTotal += p;
            }
            assertEquals(1.0, bestTotal, 1e-6);

            double threshold = runTest(trainer, 1000);
            trainer.sessionBegin(TrainerPlan.STEADY, 5000);
            assertEquals(threshold * 1.1, trainer.sessionRate(5000), 1e-9 * threshold);
            assertEquals(threshold * 1.1, trainer.sessionRate(5600), 1e-9 * threshold);
            assertTrue(trainer.addMeasure(TrainerMeasure.RATING, 0.75, 1, threshold * 1.1, 5600));
            runTest(trainer, 6000);
            int session = trainer.sessionEnd(1, 6100);
            assertTrue(session >= 0);
            assertTrue(trainer.addRetention(session, 0.8, 5, 86400, 92000));
            assertFalse(trainer.addRetention(session, 2.0, 5, 86400, 92000));
        }
    }

    private static List<TrainerPlan> plans(ListenerTrainer trainer) {
        List<TrainerPlan> plans = new ArrayList<>();
        for (int i = 0; i < 20; i++) {
            plans.add(trainer.nextPlan());
        }
        return plans;
    }

    @Test
    public void sameSeedSamePlans() {
        List<TrainerPlan> a;
        List<TrainerPlan> b;
        List<TrainerPlan> zero;
        List<TrainerPlan> implicitZero;
        try (ListenerTrainer one = new ListenerTrainer(42); ListenerTrainer two = new ListenerTrainer(42)) {
            a = plans(one);
            b = plans(two);
        }
        try (ListenerTrainer one = new ListenerTrainer(0); ListenerTrainer two = new ListenerTrainer()) {
            zero = plans(one);
            implicitZero = plans(two);
        }
        assertEquals(a, b);
        assertTrue(new HashSet<>(a).size() > 1);
        assertEquals(zero, implicitZero);
    }

    @Test
    public void aClosedTrainerThrows() {
        ListenerTrainer trainer = new ListenerTrainer();
        trainer.close();
        trainer.close();
        assertThrows(IllegalStateException.class, trainer::threshold);
        assertThrows(IllegalStateException.class, trainer::nextPlan);
    }

    @Test
    public void blindTrialsChooseAndCount() {
        try (BlindTrials trials = new BlindTrials(3)) {
            assertFalse(trials.next(5).isPresent());
            int a = trials.addSetting();
            int b = trials.addSetting();
            assertEquals(0, a);
            assertEquals(1, b);
            assertEquals(0, trials.addValue(a, 0.0));
            assertEquals(1, trials.addValue(a, 0.06));
            assertEquals(-1, trials.addValue(a, 0.06));
            assertEquals(-1, trials.addValue(9, 1.0));
            trials.setAvailable(b, false);

            BlindTrial first = trials.next(5.5).get();
            assertEquals(a, first.setting());
            assertEquals(new HashSet<>(java.util.Arrays.asList(0.0, 0.06)),
                    new HashSet<>(java.util.Arrays.asList(first.first(), first.second())));
            assertFalse(trials.meanScore(a, 5.5, 0).isPresent());
            assertFalse(trials.winner(a, 5.5).isPresent());

            // Five trials in the 5 to 6 band, each preferring neither: ties.
            for (int i = 0; i < 5; i++) {
                BlindTrial trial = trials.next(5.5).get();
                assertTrue(trials.add(trial.setting(), 5.5, trial.first(), trial.second(), 0.6, 0.6, 0));
            }
            assertEquals(5, trials.heard(a, 5.9, 0));
            assertEquals(5, trials.heard(a, 5.0, 1));
            assertEquals(5, trials.tied(a, 5.5, 0));
            assertEquals(0, trials.won(a, 5.5, 0));
            assertEquals(0, trials.lost(a, 5.5, 1));
            assertEquals(0.6, trials.meanScore(a, 5.5, 0).getAsDouble(), 1e-12);
            assertFalse(trials.winner(a, 5.5).isPresent());
            assertEquals(0, trials.heard(a, 7.5, 0)); // another band
            assertFalse(trials.add(a, 5.5, 0.0, 0.5, 0.5, 0.5, 0)); // 0.5 is not a value added
        }
    }

    @Test
    public void blindTrialsNameAWinner() {
        try (BlindTrials trials = new BlindTrials(11)) {
            int a = trials.addSetting();
            trials.addValue(a, 0.0);
            trials.addValue(a, 1.0);
            trials.setConfidence(0.9);
            for (int i = 0; i < 12; i++) {
                assertTrue(trials.add(a, 6.5, 0.0, 1.0, 0.4, 0.8, 1));
            }
            assertEquals(1, trials.winner(a, 6.5).getAsInt());
            assertEquals(12, trials.won(a, 6.5, 1));
            assertEquals(12, trials.lost(a, 6.5, 0));
            assertTrue(trials.meanScore(a, 6.5, 1).isPresent());
        }
    }

    private static List<BlindTrial> run(long seed) {
        List<BlindTrial> list = new ArrayList<>();
        try (BlindTrials trials = new BlindTrials(seed)) {
            int a = trials.addSetting();
            trials.addValue(a, 1);
            trials.addValue(a, 2);
            trials.addValue(a, 3);
            for (int i = 0; i < 10; i++) {
                Optional<BlindTrial> next = trials.next(4.5);
                BlindTrial trial = next.get();
                list.add(trial);
                trials.add(a, 4.5, trial.first(), trial.second(), 0.5, 0.5, 0);
            }
        }
        return list;
    }

    @Test
    public void sameSeedSameTrials() {
        assertEquals(run(5), run(5));
    }
}
