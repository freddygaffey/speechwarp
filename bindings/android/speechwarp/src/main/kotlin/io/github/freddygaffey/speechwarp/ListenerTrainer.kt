package io.github.freddygaffey.speechwarp

/** What a score in 0 to 1 measures. */
enum class TrainerMeasure(internal val value: Int) {
    /** Share of the words said back correctly from a sentence heard once. */
    INTELLIGIBILITY(0),

    /**
     * Share right on "was this sentence in what you just heard?" items (the sentence verification technique:
     * originals and paraphrases against changed-meaning and unrelated sentences). Chance is 0.5.
     */
    VERIFICATION(1),

    /** Verification items about a session's material, answered after a delay; see [ListenerTrainer.addRetention]. */
    RETENTION(2),

    /** The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. Weighted less by default. */
    RATING(3),
}

/** Session plans: how the rate moves during a session. */
enum class TrainerPlan(internal val value: Int) {
    /** The threshold plus a margin, all session. */
    STEADY(0),

    /** Start below the threshold and step up to threshold plus margin. */
    RAMP(1),

    /** Alternate periods above and below the threshold. */
    INTERVAL(2),

    /** Move up or down after each in-session check, to stay at the target. */
    TRACKING(3),
}

/** Tunable numbers of a [ListenerTrainer], with their defaults. */
enum class TrainerParam(internal val value: Int) {
    /** Share understood that defines the threshold: 0.75 (0.5 to 0.95). */
    TARGET(0),

    /** Steady and ramp: aim this fraction above the threshold: 0.10. */
    MARGIN(1),

    /** Ramp: start at this fraction of the target rate: 0.8. */
    RAMP_START(2),

    /** Ramp: step by this fraction of the target rate: 0.02. */
    RAMP_STEP(3),

    /** Ramp: minutes between steps: 2. */
    RAMP_MINUTES(4),

    /** Interval: this fraction above, then below, the threshold: 0.15. */
    INTERVAL_SPREAD(5),

    /** Interval: minutes in each period: 10. */
    INTERVAL_MINUTES(6),

    /** Tracking: ln(rate) moves by gain x (score - target) per check: 0.4. */
    TRACKING_GAIN(7),

    /** Plans: threshold gain a hour worth a whole unit of retention: 0.2, so 10 points of retention are worth 2% a hour. */
    RETENTION_COST(8),

    /** Threshold test: most presentations: 40. */
    TEST_MAX(9),

    /** Threshold test: done when the 95% interval's high / low is below this: 1.25. */
    TEST_PRECISION(10),
}

/**
 * Pure logic for training a listener to follow faster speech: no audio, no clock, no storage. The caller passes
 * plain numbers in (scores, rates, its own timestamps) and gets rates and plans out. Deterministic: two trainers
 * created with the same seed and given the same calls give the same answers, so a caller keeps its own log of
 * calls and replays it into a new trainer to restore state.
 *
 * The unit of rate everywhere is syllables a second heard: the source's syllable rate ([SpeechwarpStream.syllableRate]
 * or [SyllableCounter.rate]) times the speed. Times are seconds on any clock the caller likes (Unix time, say),
 * and only differences are used. NaN arguments are ignored. Values that are NaN without data are returned as NaN.
 *
 * The protocol: a threshold test ([testBegin], [testRate], [addMeasure], [testEnd]), [sessionBegin], listening
 * with a check every ten minutes or so ([addMeasure]), a second test, [sessionEnd]; retention items a day and a
 * week later ([addRetention]).
 *
 * A trainer is not thread safe. [close] it when done.
 *
 * @param seed the seed for the random choices, as the bit pattern of an unsigned 64-bit number
 */
class ListenerTrainer(seed: Long = 0) : AutoCloseable {
    private var handle: Long

    init {
        handle = nativeCreate(seed)
        if (handle == 0L) throw OutOfMemoryError("speechwarp could not allocate a trainer")
    }

    /**
     * How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification 1,
     * retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
     */
    fun setWeight(kind: TrainerMeasure, weight: Double) = nativeSetWeight(open(), kind.value, weight)

    /** The weight of a kind of measure; see [setWeight]. */
    fun getWeight(kind: TrainerMeasure): Double = nativeGetWeight(open(), kind.value)

    /** Sets a tunable number; out-of-range values are clamped and NaN is ignored. */
    fun setParam(param: TrainerParam, value: Double) = nativeSetParam(open(), param.value, value)

    /** The value of a tunable number. */
    fun getParam(param: TrainerParam): Double = nativeGetParam(open(), param.value)

    /**
     * Adds a score: [kind] (not [TrainerMeasure.RETENTION]), [score] 0..1, from [items] items (for a sentence
     * repeated back, the number of words scored; for verification, the number of questions; for a rating, 1),
     * heard at [rate] syllables a second, at [time]. During a threshold test it updates the estimate; during a
     * session it is an in-session check, and the tracking plan reacts to it.
     *
     * @return false if an argument is invalid
     */
    fun addMeasure(kind: TrainerMeasure, score: Double, items: Double, rate: Double, time: Double): Boolean =
        nativeAddMeasure(open(), kind.value, score, items, rate, time)

    /**
     * Starts a threshold test: an estimate of the rate understood [TrainerParam.TARGET] (75%) of the time, by the
     * psi method (a Bayesian posterior over the threshold and the slope of a logistic psychometric function in
     * log rate). The prior is log-normal around [priorRate] or, if that is 0, around the last estimate, or
     * failing that around 10 syllables a second, within 3 to 60.
     */
    fun testBegin(priorRate: Double, time: Double) = nativeTestBegin(open(), priorRate, time)

    /** The rate to present next in the test. */
    fun testRate(): Double = nativeTestRate(open())

    /**
     * True once the 95% interval is narrower than [TrainerParam.TEST_PRECISION] (at least 8 presentations) or
     * [TrainerParam.TEST_MAX] presentations have been scored; false otherwise or if no test is running.
     */
    val testDone: Boolean
        get() = nativeTestDone(open())

    /** Finishes the test; the estimate becomes the current threshold. @return it (0 if no test was running) */
    fun testEnd(time: Double): Double = nativeTestEnd(open(), time)

    /** The current estimate (posterior median) of the running test, else of the last one finished; 0 if none. */
    val threshold: Double
        get() = nativeThreshold(open())

    /** The low end of the 95% interval of [threshold]; 0 if none. */
    val thresholdLow: Double
        get() = nativeThresholdLow(open())

    /** The high end of the 95% interval of [threshold]; 0 if none. */
    val thresholdHigh: Double
        get() = nativeThresholdHigh(open())

    /** Begins a session under [plan], from the threshold at this moment. */
    fun sessionBegin(plan: TrainerPlan, time: Double) = nativeSessionBegin(open(), plan.value, time)

    /** The rate to play at now, under the session's plan; 0 if no session. */
    fun sessionRate(time: Double): Double = nativeSessionRate(open(), time)

    /**
     * Ends the session after [listeningHours] of listening in it. It is recorded for comparing plans if a
     * threshold test ended after it began.
     *
     * @return the session's number (0, 1, ...) for [addRetention], or -1
     */
    fun sessionEnd(listeningHours: Double, time: Double): Int = nativeSessionEnd(open(), listeningHours, time)

    /**
     * Adds retention for a recorded [session]: [score] 0..1 from [items] items, answered [delaySeconds] after it
     * ended.
     *
     * @return false if an argument is invalid
     */
    fun addRetention(session: Int, score: Double, items: Double, delaySeconds: Double, time: Double): Boolean =
        nativeAddRetention(open(), session, score, items, delaySeconds, time)

    /**
     * The plan to run next: a Thompson draw over a Bayesian model of each recorded session's threshold gain
     * (advances the random source).
     */
    fun nextPlan(): TrainerPlan {
        val plan = nativeNextPlan(open())
        return TrainerPlan.entries.first { it.value == plan }
    }

    /** A plan's estimated threshold gain a hour now, as a fraction (0.01 is 1% a hour). */
    fun planEffect(plan: TrainerPlan): Double = nativePlanEffect(open(), plan.value)

    /** The standard deviation of [planEffect]. */
    fun planEffectSd(plan: TrainerPlan): Double = nativePlanEffectSd(open(), plan.value)

    /** A plan's retention; NaN without data. */
    fun planRetention(plan: TrainerPlan): Double = nativePlanRetention(open(), plan.value)

    /** The standard deviation of [planRetention]; NaN without data. */
    fun planRetentionSd(plan: TrainerPlan): Double = nativePlanRetentionSd(open(), plan.value)

    /** The number of sessions recorded under a plan. */
    fun planSessions(plan: TrainerPlan): Int = nativePlanSessions(open(), plan.value)

    /** The probability that a plan is the best by utility, from a fixed number of draws (does not advance the random source). */
    fun planBestProbability(plan: TrainerPlan): Double = nativePlanBestProbability(open(), plan.value)

    /** The trend: the hours of listening by which gains have halved (1000 stands for "not slowing"). */
    val trend: Double
        get() = nativeTrend(open())

    /** The uncertainty of [trend] as a standard deviation of ln H. */
    val trendSd: Double
        get() = nativeTrendSd(open())

    /** Frees the native trainer. Using it afterwards throws [IllegalStateException]. */
    override fun close() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0
        }
    }

    protected fun finalize() = close()

    private fun open(): Long {
        check(handle != 0L) { "the trainer is closed" }
        return handle
    }

    private companion object {
        init {
            System.loadLibrary("speechwarp_jni")
        }

        @JvmStatic private external fun nativeCreate(seed: Long): Long
        @JvmStatic private external fun nativeDestroy(handle: Long)
        @JvmStatic private external fun nativeSetWeight(handle: Long, kind: Int, weight: Double)
        @JvmStatic private external fun nativeGetWeight(handle: Long, kind: Int): Double
        @JvmStatic private external fun nativeSetParam(handle: Long, param: Int, value: Double)
        @JvmStatic private external fun nativeGetParam(handle: Long, param: Int): Double
        @JvmStatic private external fun nativeAddMeasure(handle: Long, kind: Int, score: Double, items: Double, rate: Double, time: Double): Boolean
        @JvmStatic private external fun nativeTestBegin(handle: Long, priorRate: Double, time: Double)
        @JvmStatic private external fun nativeTestRate(handle: Long): Double
        @JvmStatic private external fun nativeTestDone(handle: Long): Boolean
        @JvmStatic private external fun nativeTestEnd(handle: Long, time: Double): Double
        @JvmStatic private external fun nativeThreshold(handle: Long): Double
        @JvmStatic private external fun nativeThresholdLow(handle: Long): Double
        @JvmStatic private external fun nativeThresholdHigh(handle: Long): Double
        @JvmStatic private external fun nativeSessionBegin(handle: Long, plan: Int, time: Double)
        @JvmStatic private external fun nativeSessionRate(handle: Long, time: Double): Double
        @JvmStatic private external fun nativeSessionEnd(handle: Long, hours: Double, time: Double): Int
        @JvmStatic private external fun nativeAddRetention(handle: Long, session: Int, score: Double, items: Double, delaySeconds: Double, time: Double): Boolean
        @JvmStatic private external fun nativeNextPlan(handle: Long): Int
        @JvmStatic private external fun nativePlanEffect(handle: Long, plan: Int): Double
        @JvmStatic private external fun nativePlanEffectSd(handle: Long, plan: Int): Double
        @JvmStatic private external fun nativePlanRetention(handle: Long, plan: Int): Double
        @JvmStatic private external fun nativePlanRetentionSd(handle: Long, plan: Int): Double
        @JvmStatic private external fun nativePlanSessions(handle: Long, plan: Int): Int
        @JvmStatic private external fun nativePlanBestProbability(handle: Long, plan: Int): Double
        @JvmStatic private external fun nativeTrend(handle: Long): Double
        @JvmStatic private external fun nativeTrendSd(handle: Long): Double
    }
}
