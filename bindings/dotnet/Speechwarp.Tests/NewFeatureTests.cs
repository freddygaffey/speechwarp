using Speechwarp;

namespace Speechwarp.Tests;

[TestClass]
public class HeardPauseAndFloorBlendTests
{
    [TestMethod]
    public void HeardPauseReadsBackAndSetsThePauseCap()
    {
        using var stream = new SpeechwarpStream(22050, 1);
        Assert.AreEqual(0f, stream.HeardPause);
        stream.SetHeardPause(0.03f, 3f);
        Assert.AreEqual(0.03f, stream.HeardPause);
        Assert.AreEqual(3f, stream.HeardPauseFrom);
        stream.Speed = 5;
        Assert.AreEqual(0.15f, stream.PauseCap, 1e-5f);
        stream.Speed = 2;
        Assert.AreEqual(0f, stream.PauseCap);
        stream.SetHeardPause(0, 3f);
        Assert.AreEqual(0f, stream.HeardPause);
    }

    [TestMethod]
    public void FloorBlendReadsBackAndRisesWithSpeed()
    {
        using var stream = new SpeechwarpStream(22050, 1);
        stream.SetFloorBlend(0.5f, 4f, 6f);
        Assert.AreEqual(0.5f, stream.FloorBlend);
        Assert.AreEqual(4f, stream.FloorBlendFrom);
        Assert.AreEqual(6f, stream.FloorBlendFull);
        stream.Speed = 3;
        Assert.AreEqual(0f, stream.SpeedFloor);
        stream.Speed = 5;
        Assert.AreEqual(0.25f, stream.SpeedFloor, 1e-5f);
        stream.Speed = 8;
        Assert.AreEqual(0.5f, stream.SpeedFloor, 1e-5f);
    }
}

[TestClass]
public class SyllableCounterTests
{
    private const int Rate = 22050;

    private static float[] Signal(double seconds)
    {
        int frames = (int)(Rate * seconds);
        var samples = new float[frames];
        double phase = 0;
        for (int i = 0; i < frames; i++)
        {
            double t = (double)i / Rate;
            phase += 2 * Math.PI * (120 + 60 * Math.Sin(t * 3)) / Rate;
            double envelope = t % 0.4 < 0.3 ? Math.Sin(Math.PI * (t % 0.4) / 0.3) : 0;
            samples[i] = (float)(0.4 * envelope * (Math.Sin(phase) + 0.5 * Math.Sin(2 * phase) + 0.3 * Math.Sin(3 * phase)));
        }
        return samples;
    }

    [TestMethod]
    public void NullBeforeTheMinimumThenMatchesAStream()
    {
        float[] input = Signal(12);
        using var counter = new SyllableCounter(Rate, 1);
        using var stream = new SpeechwarpStream(Rate, 1);
        counter.Write(input.AsSpan(0, Rate * 3));
        Assert.IsNull(counter.Rate());
        counter.Reset();
        counter.Write(input);
        stream.Write(input);

        double? rate = counter.Rate();
        Assert.IsNotNull(rate);
        Assert.IsGreaterThan(0, rate.Value);
        Assert.AreEqual(stream.SyllableRate, rate);
        Assert.IsNotNull(counter.Rate(5, 2));
    }

    [TestMethod]
    public void Int16InputAndBadArguments()
    {
        using var counter = new SyllableCounter(Rate, 2);
        counter.Write(new short[2000]);
        Assert.IsNull(counter.Rate());
        Assert.ThrowsExactly<ArgumentException>(() => counter.Write(new float[3]));
        Assert.ThrowsExactly<ArgumentOutOfRangeException>(() => new SyllableCounter(100, 1));
        counter.Dispose();
        Assert.ThrowsExactly<ObjectDisposedException>(() => counter.Reset());
    }
}

[TestClass]
public class ListenerTrainerTests
{
    private static ListenerTrainer RunTest(ulong seed)
    {
        var trainer = new ListenerTrainer(seed);
        trainer.TestBegin(10, 0);
        double time = 0;
        for (int i = 0; i < 12 && !trainer.TestDone; i++)
        {
            double rate = trainer.TestRate();
            // A listener whose understanding falls with rate, with threshold 12.
            double score = Math.Clamp(1 - 0.5 * (rate / 12) * (rate / 12), 0, 1);
            Assert.IsTrue(trainer.AddMeasure(TrainerMeasure.Intelligibility, score, 8, rate, time += 20));
        }
        trainer.TestEnd(time);
        return trainer;
    }

    [TestMethod]
    public void ThresholdTestGivesAnOrderedInterval()
    {
        using var trainer = RunTest(1);
        Assert.IsGreaterThan(0, trainer.Threshold);
        Assert.IsLessThan(trainer.Threshold, trainer.ThresholdLow);
        Assert.IsGreaterThan(trainer.Threshold, trainer.ThresholdHigh);
    }

    [TestMethod]
    public void SameSeedSamePlansAndSteadyRate()
    {
        using var a = RunTest(7);
        using var b = RunTest(7);
        for (int i = 0; i < 10; i++)
            Assert.AreEqual(a.NextPlan(), b.NextPlan());

        a.SessionBegin(TrainerPlan.Steady, 1000);
        Assert.AreEqual(a.Threshold * 1.1, a.SessionRate(1000), 1e-9);
        Assert.AreEqual(0, a.SessionEnd(0.5, 2000));
    }

    [TestMethod]
    public void RetentionTrendAndPlanStatisticsAreReadable()
    {
        using var trainer = RunTest(3);
        trainer.SessionBegin(TrainerPlan.Ramp, 1000);
        // A second threshold test, ended after the session began, makes the session count for plan comparison.
        trainer.TestBegin(trainer.Threshold, 1500);
        double t = 1500;
        for (int i = 0; i < 12 && !trainer.TestDone; i++)
        {
            double rate = trainer.TestRate();
            double score = Math.Clamp(1 - 0.5 * (rate / 13) * (rate / 13), 0, 1);
            trainer.AddMeasure(TrainerMeasure.Intelligibility, score, 8, rate, t += 20);
        }
        trainer.TestEnd(t);
        int session = trainer.SessionEnd(1.0, 2000);
        Assert.IsGreaterThanOrEqualTo(0, session);
        Assert.IsTrue(trainer.AddRetention(session, 0.8, 10, 86400, 90000));
        Assert.IsFalse(trainer.AddRetention(session, 2, 10, 86400, 90000));
        Assert.AreEqual(1, trainer.PlanSessions(TrainerPlan.Ramp));
        Assert.IsFalse(double.IsNaN(trainer.PlanRetention(TrainerPlan.Ramp)));
        Assert.IsGreaterThanOrEqualTo(0.0, trainer.PlanRetentionSd(TrainerPlan.Ramp));
        Assert.IsGreaterThanOrEqualTo(0.0, trainer.PlanEffectSd(TrainerPlan.Ramp));
        double p = trainer.PlanBestProbability(TrainerPlan.Ramp);
        Assert.IsTrue(p >= 0 && p <= 1);
        Assert.IsFalse(double.IsNaN(trainer.Trend));
        Assert.IsFalse(double.IsNaN(trainer.TrendSd));
    }

    [TestMethod]
    public void ParamsWeightsAndEmptyPlanStatistics()
    {
        using var trainer = new ListenerTrainer();
        Assert.AreEqual(0.75, trainer.GetParam(TrainerParam.Target));
        trainer.SetParam(TrainerParam.Margin, 0.2);
        Assert.AreEqual(0.2, trainer.GetParam(TrainerParam.Margin));
        trainer.SetWeight(TrainerMeasure.Rating, 0.5);
        Assert.AreEqual(0.5, trainer.GetWeight(TrainerMeasure.Rating));
        Assert.AreEqual(0.0, trainer.PlanEffect(TrainerPlan.Ramp));
        Assert.IsTrue(double.IsNaN(trainer.PlanRetention(TrainerPlan.Ramp)));
        Assert.AreEqual(0, trainer.PlanSessions(TrainerPlan.Ramp));
        Assert.IsFalse(trainer.AddMeasure(TrainerMeasure.Intelligibility, 2, 8, 10, 0));
    }
}

[TestClass]
public class BlindTrialsTests
{
    [TestMethod]
    public void NextRecordsAndCountsAddUp()
    {
        using var trials = new BlindTrials(3);
        Assert.IsNull(trials.Next(5));
        int setting = trials.AddSetting();
        Assert.AreEqual(0, setting);
        Assert.AreEqual(0, trials.AddValue(setting, 0.3));
        Assert.AreEqual(1, trials.AddValue(setting, 0.6));

        var next = trials.Next(5.5);
        Assert.IsNotNull(next);
        Assert.AreEqual(setting, next.Value.Setting);
        CollectionAssert.AreEquivalent(new[] { 0.3, 0.6 }, new[] { next.Value.First, next.Value.Second });

        Assert.IsNull(trials.MeanScore(setting, 5.5, 0));
        Assert.IsNull(trials.Winner(setting, 5.5));
        for (int i = 0; i < 6; i++)
        {
            Assert.IsTrue(trials.Add(setting, 5.5, 0.3, 0.6, 0.5, 0.5, 0));
        }
        Assert.IsFalse(trials.Add(setting, 5.5, 0.3, 9, 0.5, 0.5, 0));
        Assert.AreEqual(6, trials.Tied(setting, 5.5, 0));
        Assert.AreEqual(0, trials.Won(setting, 5.5, 0));
        Assert.AreEqual(0, trials.Lost(setting, 5.5, 1));
        Assert.AreEqual(6, trials.Heard(setting, 5.5, 1));
        Assert.AreEqual(0.5, trials.MeanScore(setting, 5.5, 1));
        Assert.IsNull(trials.Winner(setting, 5.5));

        trials.SetAvailable(setting, false);
        Assert.IsNull(trials.Next(5.5));
        trials.SetAvailable(setting, true);
        trials.SetConfidence(0.9);
    }

    [TestMethod]
    public void ARepeatedWinnerIsNamed()
    {
        using var trials = new BlindTrials();
        int s = trials.AddSetting();
        trials.AddValue(s, 1);
        trials.AddValue(s, 2);
        for (int i = 0; i < 12; i++)
            trials.Add(s, 4.5, 1, 2, 0.9, 0.5, -1);
        Assert.AreEqual(0, trials.Winner(s, 4.5));
    }
}
