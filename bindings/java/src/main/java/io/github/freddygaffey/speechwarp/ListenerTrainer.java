package io.github.freddygaffey.speechwarp;

/**
 * Pure logic for training a listener to follow faster speech: no audio, no clock, no storage. The caller passes
 * plain numbers in (scores, rates, its own timestamps) and gets rates and plans out. Deterministic: two trainers
 * created with the same seed and given the same calls give the same answers, so a caller keeps its own log of
 * calls and replays it into a new trainer to restore state.
 *
 * <p>The unit of rate everywhere is syllables a second heard: the source's syllable rate ({@link
 * SpeechwarpStream#syllableRate()} or {@link SyllableCounter#rate()}) times the speed. Times are seconds on any
 * clock the caller likes (Unix time, say), and only differences are used. NaN arguments are ignored. Values that
 * are NaN without data are returned as NaN.
 *
 * <p>The protocol: a threshold test ({@link #testBegin}, {@link #testRate}, {@link #addMeasure}, {@link
 * #testEnd}), {@link #sessionBegin}, listening with a check every ten minutes or so ({@link #addMeasure}), a
 * second test, {@link #sessionEnd}; retention items a day and a week later ({@link #addRetention}).
 *
 * <p>A trainer is not thread safe. {@link #close()} it when done.
 */
public final class ListenerTrainer implements AutoCloseable {
    private long handle;

    static {
        NativeLibrary.ensureLoaded();
    }

    /** Creates a trainer with seed 0. */
    public ListenerTrainer() {
        this(0);
    }

    /**
     * Creates a trainer.
     *
     * @param seed the seed for the random choices, as the bit pattern of an unsigned 64-bit number
     */
    public ListenerTrainer(long seed) {
        handle = nativeCreate(seed);
        if (handle == 0) {
            throw new OutOfMemoryError("speechwarp could not allocate a trainer");
        }
    }

    /**
     * How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification 1,
     * retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
     */
    public void setWeight(TrainerMeasure kind, double weight) {
        nativeSetWeight(open(), kind.value(), weight);
    }

    /** The weight of a kind of measure; see {@link #setWeight}. */
    public double getWeight(TrainerMeasure kind) {
        return nativeGetWeight(open(), kind.value());
    }

    /** Sets a tunable number; out-of-range values are clamped and NaN is ignored. */
    public void setParam(TrainerParam param, double value) {
        nativeSetParam(open(), param.value(), value);
    }

    /** The value of a tunable number. */
    public double getParam(TrainerParam param) {
        return nativeGetParam(open(), param.value());
    }

    /**
     * Adds a score: {@code kind} (not {@link TrainerMeasure#RETENTION}), {@code score} 0..1, from {@code items}
     * items (for a sentence repeated back, the number of words scored; for verification, the number of
     * questions; for a rating, 1), heard at {@code rate} syllables a second, at {@code time}. During a threshold
     * test it updates the estimate; during a session it is an in-session check, and the tracking plan reacts to
     * it.
     *
     * @return false if an argument is invalid
     */
    public boolean addMeasure(TrainerMeasure kind, double score, double items, double rate, double time) {
        return nativeAddMeasure(open(), kind.value(), score, items, rate, time);
    }

    /**
     * Starts a threshold test: an estimate of the rate understood {@link TrainerParam#TARGET} (75%) of the time,
     * by the psi method (a Bayesian posterior over the threshold and the slope of a logistic psychometric
     * function in log rate). The prior is log-normal around {@code priorRate} or, if that is 0, around the last
     * estimate, or failing that around 10 syllables a second, within 3 to 60.
     */
    public void testBegin(double priorRate, double time) {
        nativeTestBegin(open(), priorRate, time);
    }

    /** The rate to present next in the test. */
    public double testRate() {
        return nativeTestRate(open());
    }

    /**
     * True once the 95% interval is narrower than {@link TrainerParam#TEST_PRECISION} (at least 8 presentations)
     * or {@link TrainerParam#TEST_MAX} presentations have been scored; false otherwise or if no test is running.
     */
    public boolean testDone() {
        return nativeTestDone(open());
    }

    /** Finishes the test; the estimate becomes the current threshold. Returns it (0 if no test was running). */
    public double testEnd(double time) {
        return nativeTestEnd(open(), time);
    }

    /** The current estimate (posterior median) of the running test, else of the last one finished; 0 if none. */
    public double threshold() {
        return nativeThreshold(open());
    }

    /** The low end of the 95% interval of {@link #threshold()}; 0 if none. */
    public double thresholdLow() {
        return nativeThresholdLow(open());
    }

    /** The high end of the 95% interval of {@link #threshold()}; 0 if none. */
    public double thresholdHigh() {
        return nativeThresholdHigh(open());
    }

    /** Begins a session under {@code plan}, from the threshold at this moment. */
    public void sessionBegin(TrainerPlan plan, double time) {
        nativeSessionBegin(open(), plan.value(), time);
    }

    /** The rate to play at now, under the session's plan; 0 if no session. */
    public double sessionRate(double time) {
        return nativeSessionRate(open(), time);
    }

    /**
     * Ends the session after {@code listeningHours} of listening in it. It is recorded for comparing plans if a
     * threshold test ended after it began.
     *
     * @return the session's number (0, 1, ...) for {@link #addRetention}, or -1
     */
    public int sessionEnd(double listeningHours, double time) {
        return nativeSessionEnd(open(), listeningHours, time);
    }

    /**
     * Adds retention for a recorded session: {@code score} 0..1 from {@code items} items, answered {@code
     * delaySeconds} after it ended.
     *
     * @return false if an argument is invalid
     */
    public boolean addRetention(int session, double score, double items, double delaySeconds, double time) {
        return nativeAddRetention(open(), session, score, items, delaySeconds, time);
    }

    /**
     * The plan to run next: a Thompson draw over a Bayesian model of each recorded session's threshold gain
     * (advances the random source).
     */
    public TrainerPlan nextPlan() {
        return TrainerPlan.of(nativeNextPlan(open()));
    }

    /** A plan's estimated threshold gain a hour now, as a fraction (0.01 is 1% a hour). */
    public double planEffect(TrainerPlan plan) {
        return nativePlanEffect(open(), plan.value());
    }

    /** The standard deviation of {@link #planEffect}. */
    public double planEffectSd(TrainerPlan plan) {
        return nativePlanEffectSd(open(), plan.value());
    }

    /** A plan's retention; NaN without data. */
    public double planRetention(TrainerPlan plan) {
        return nativePlanRetention(open(), plan.value());
    }

    /** The standard deviation of {@link #planRetention}; NaN without data. */
    public double planRetentionSd(TrainerPlan plan) {
        return nativePlanRetentionSd(open(), plan.value());
    }

    /** The number of sessions recorded under a plan. */
    public int planSessions(TrainerPlan plan) {
        return nativePlanSessions(open(), plan.value());
    }

    /**
     * The probability that a plan is the best by utility, from a fixed number of draws (does not advance the
     * random source).
     */
    public double planBestProbability(TrainerPlan plan) {
        return nativePlanBestProbability(open(), plan.value());
    }

    /** The trend: the hours of listening by which gains have halved (1000 stands for "not slowing"). */
    public double trend() {
        return nativeTrend(open());
    }

    /** The uncertainty of {@link #trend()} as a standard deviation of ln H. */
    public double trendSd() {
        return nativeTrendSd(open());
    }

    /** Frees the native trainer. Using it afterwards throws {@link IllegalStateException}. */
    @Override
    public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private long open() {
        if (handle == 0) {
            throw new IllegalStateException("the trainer is closed");
        }
        return handle;
    }

    private static native long nativeCreate(long seed);
    private static native void nativeDestroy(long handle);
    private static native void nativeSetWeight(long handle, int kind, double weight);
    private static native double nativeGetWeight(long handle, int kind);
    private static native void nativeSetParam(long handle, int param, double value);
    private static native double nativeGetParam(long handle, int param);
    private static native boolean nativeAddMeasure(long handle, int kind, double score, double items, double rate, double time);
    private static native void nativeTestBegin(long handle, double priorRate, double time);
    private static native double nativeTestRate(long handle);
    private static native boolean nativeTestDone(long handle);
    private static native double nativeTestEnd(long handle, double time);
    private static native double nativeThreshold(long handle);
    private static native double nativeThresholdLow(long handle);
    private static native double nativeThresholdHigh(long handle);
    private static native void nativeSessionBegin(long handle, int plan, double time);
    private static native double nativeSessionRate(long handle, double time);
    private static native int nativeSessionEnd(long handle, double hours, double time);
    private static native boolean nativeAddRetention(long handle, int session, double score, double items, double delaySeconds, double time);
    private static native int nativeNextPlan(long handle);
    private static native double nativePlanEffect(long handle, int plan);
    private static native double nativePlanEffectSd(long handle, int plan);
    private static native double nativePlanRetention(long handle, int plan);
    private static native double nativePlanRetentionSd(long handle, int plan);
    private static native int nativePlanSessions(long handle, int plan);
    private static native double nativePlanBestProbability(long handle, int plan);
    private static native double nativeTrend(long handle);
    private static native double nativeTrendSd(long handle);
}
