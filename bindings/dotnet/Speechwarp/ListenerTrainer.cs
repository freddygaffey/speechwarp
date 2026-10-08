using System;

namespace Speechwarp;

/// <summary>The kind of a score given to <see cref="ListenerTrainer.AddMeasure"/>.</summary>
public enum TrainerMeasure
{
    /// <summary>Share of words understood.</summary>
    Intelligibility = 0,
    /// <summary>Answers to verification questions (guess rate 0.5).</summary>
    Verification = 1,
    /// <summary>Retention; given with <see cref="ListenerTrainer.AddRetention"/>, not used for thresholds.</summary>
    Retention = 2,
    /// <summary>A subjective rating.</summary>
    Rating = 3,
}

/// <summary>A way of choosing the rate through a listening session.</summary>
public enum TrainerPlan
{
    /// <summary>The threshold plus a margin, all session.</summary>
    Steady = 0,
    /// <summary>Start below the threshold and step up to threshold plus margin.</summary>
    Ramp = 1,
    /// <summary>Alternate periods above and below the threshold.</summary>
    Interval = 2,
    /// <summary>Move up or down after each in-session check, to stay at the target.</summary>
    Tracking = 3,
}

/// <summary>Tunable numbers of a <see cref="ListenerTrainer"/>, with their defaults.</summary>
public enum TrainerParam
{
    /// <summary>Share understood that defines the threshold: 0.75 (0.5 to 0.95).</summary>
    Target = 0,
    /// <summary>Steady and ramp: aim this fraction above the threshold: 0.10.</summary>
    Margin = 1,
    /// <summary>Ramp: start at this fraction of the target rate: 0.8.</summary>
    RampStart = 2,
    /// <summary>Ramp: step by this fraction of the target rate: 0.02.</summary>
    RampStep = 3,
    /// <summary>Ramp: minutes between steps: 2.</summary>
    RampMinutes = 4,
    /// <summary>Interval: this fraction above, then below, the threshold: 0.15.</summary>
    IntervalSpread = 5,
    /// <summary>Interval: minutes in each period: 10.</summary>
    IntervalMinutes = 6,
    /// <summary>Tracking: ln(rate) moves by gain x (score - target) per check: 0.4.</summary>
    TrackingGain = 7,
    /// <summary>Plans: threshold gain a hour (as a fraction, before practice slows it) worth a whole unit of retention: 0.2, so 10 points of retention are worth 2% a hour.</summary>
    RetentionCost = 8,
    /// <summary>Threshold test: most presentations: 40.</summary>
    TestMax = 9,
    /// <summary>Threshold test: done when the 95% interval's high / low is below this: 1.25.</summary>
    TestPrecision = 10,
}

/// <summary>
/// Pure logic for training a listener to follow faster speech: no audio, no clock, no storage. Pass plain numbers
/// in (scores, rates, your own timestamps) and get rates and plans out. Rates are syllables a second. Two
/// trainers created with the same seed and given the same calls give the same answers, so keep a log of calls and
/// replay it into a new trainer to restore state.
/// </summary>
/// <remarks>Not thread safe. Use it from one thread, or lock around it.</remarks>
public sealed class ListenerTrainer : IDisposable
{
    private readonly TrainerHandle _handle;

    /// <summary>Creates a trainer.</summary>
    /// <param name="seed">Seeds its random choices.</param>
    /// <exception cref="OutOfMemoryException">The native object could not be allocated.</exception>
    public ListenerTrainer(ulong seed = 0)
    {
        nint trainer = Native.speechwarp_trainer_create(seed);
        if (trainer == 0)
            throw new OutOfMemoryException("speechwarp could not allocate a trainer.");
        _handle = new TrainerHandle(trainer);
    }

    /// <summary>
    /// How much a measure of each kind counts, per item, against the others. Defaults: intelligibility 0.5,
    /// verification 1, retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
    /// </summary>
    public void SetWeight(TrainerMeasure kind, double weight) =>
        Native.speechwarp_trainer_set_weight(_handle, (int)kind, weight);

    /// <summary>The weight of a kind of measure.</summary>
    public double GetWeight(TrainerMeasure kind) => Native.speechwarp_trainer_get_weight(_handle, (int)kind);

    /// <summary>Sets a tunable number.</summary>
    public void SetParam(TrainerParam param, double value) => Native.speechwarp_trainer_set_param(_handle, (int)param, value);

    /// <summary>Reads a tunable number.</summary>
    public double GetParam(TrainerParam param) => Native.speechwarp_trainer_get_param(_handle, (int)param);

    /// <summary>
    /// Adds a score. During a threshold test it updates the estimate; during a session it is an in-session check,
    /// and the tracking plan reacts to it.
    /// </summary>
    /// <param name="kind">Not <see cref="TrainerMeasure.Retention"/>; see <see cref="AddRetention"/>.</param>
    /// <param name="score">0 to 1.</param>
    /// <param name="items">Items scored: the words of a sentence repeated back, the questions of a verification, 1 for a rating.</param>
    /// <param name="rate">Syllables a second the item was heard at.</param>
    /// <param name="time">Your timestamp, in seconds.</param>
    /// <returns>False if an argument was invalid.</returns>
    public bool AddMeasure(TrainerMeasure kind, double score, double items, double rate, double time) =>
        Native.speechwarp_trainer_add_measure(_handle, (int)kind, score, items, rate, time) != 0;

    /// <summary>
    /// Starts a threshold test (the psi method). The prior is log-normal around <paramref name="priorRate"/> or, if
    /// that is 0, around the last estimate, or failing that around 10 syllables a second.
    /// </summary>
    public void TestBegin(double priorRate, double time) =>
        Native.speechwarp_trainer_test_begin(_handle, priorRate, time);

    /// <summary>The rate to present next in the running test.</summary>
    public double TestRate() => Native.speechwarp_trainer_test_rate(_handle);

    /// <summary>
    /// True once the 95% interval is narrower than <see cref="TrainerParam.TestPrecision"/> (at least 8
    /// presentations) or <see cref="TrainerParam.TestMax"/> presentations have been scored; false otherwise or if no
    /// test is running.
    /// </summary>
    public bool TestDone => Native.speechwarp_trainer_test_done(_handle) != 0;

    /// <summary>Finishes the test; the estimate becomes the current threshold.</summary>
    /// <returns>The threshold, or 0 if no test was running.</returns>
    public double TestEnd(double time) => Native.speechwarp_trainer_test_end(_handle, time);

    /// <summary>The current threshold estimate (posterior median), of the running test, else of the last one finished; 0 if none.</summary>
    public double Threshold => Native.speechwarp_trainer_threshold(_handle);

    /// <summary>The low end of the 95% interval of <see cref="Threshold"/>.</summary>
    public double ThresholdLow => Native.speechwarp_trainer_threshold_low(_handle);

    /// <summary>The high end of the 95% interval of <see cref="Threshold"/>.</summary>
    public double ThresholdHigh => Native.speechwarp_trainer_threshold_high(_handle);

    /// <summary>Starts a session under a plan.</summary>
    public void SessionBegin(TrainerPlan plan, double time) =>
        Native.speechwarp_trainer_session_begin(_handle, (int)plan, time);

    /// <summary>The rate to play at now, under the session's plan, from the threshold at <see cref="SessionBegin"/>. 0 if no session.</summary>
    public double SessionRate(double time) => Native.speechwarp_trainer_session_rate(_handle, time);

    /// <summary>
    /// Ends the session after <paramref name="listeningHours"/> of listening in it. It is recorded for comparing
    /// plans if a threshold test ended after it began.
    /// </summary>
    /// <returns>The session's number (0, 1, ...) for <see cref="AddRetention"/>, or -1.</returns>
    public int SessionEnd(double listeningHours, double time) =>
        Native.speechwarp_trainer_session_end(_handle, listeningHours, time);

    /// <summary>Adds retention for a recorded session.</summary>
    /// <param name="session">The number <see cref="SessionEnd"/> returned.</param>
    /// <param name="score">0 to 1.</param>
    /// <param name="items">Items scored.</param>
    /// <param name="delaySeconds">Seconds after the session ended that the items were answered.</param>
    /// <param name="time">Your timestamp, in seconds.</param>
    /// <returns>False if an argument was invalid.</returns>
    public bool AddRetention(int session, double score, double items, double delaySeconds, double time) =>
        Native.speechwarp_trainer_add_retention(_handle, session, score, items, delaySeconds, time) != 0;

    /// <summary>The plan to run next: a Thompson draw (advances the random source).</summary>
    public TrainerPlan NextPlan() => (TrainerPlan)Native.speechwarp_trainer_next_plan(_handle);

    /// <summary>A plan's estimated threshold gain a hour, as a fraction (0.01 is 1% a hour).</summary>
    public double PlanEffect(TrainerPlan plan) => Native.speechwarp_trainer_plan_effect(_handle, (int)plan);

    /// <summary>The standard deviation of <see cref="PlanEffect"/>.</summary>
    public double PlanEffectSd(TrainerPlan plan) => Native.speechwarp_trainer_plan_effect_sd(_handle, (int)plan);

    /// <summary>A plan's retention; NaN without data.</summary>
    public double PlanRetention(TrainerPlan plan) => Native.speechwarp_trainer_plan_retention(_handle, (int)plan);

    /// <summary>The standard deviation of <see cref="PlanRetention"/>; NaN without data.</summary>
    public double PlanRetentionSd(TrainerPlan plan) => Native.speechwarp_trainer_plan_retention_sd(_handle, (int)plan);

    /// <summary>Sessions recorded under a plan.</summary>
    public int PlanSessions(TrainerPlan plan) => Native.speechwarp_trainer_plan_sessions(_handle, (int)plan);

    /// <summary>The probability the plan is the best by utility (does not advance the random source).</summary>
    public double PlanBestProbability(TrainerPlan plan) => Native.speechwarp_trainer_plan_best_probability(_handle, (int)plan);

    /// <summary>H, the hours of listening by which gains have halved (1000 stands for "not slowing").</summary>
    public double Trend => Native.speechwarp_trainer_trend(_handle);

    /// <summary>The uncertainty of <see cref="Trend"/>, as a standard deviation of ln H.</summary>
    public double TrendSd => Native.speechwarp_trainer_trend_sd(_handle);

    /// <summary>Frees the native object.</summary>
    public void Dispose() => _handle.Dispose();
}
