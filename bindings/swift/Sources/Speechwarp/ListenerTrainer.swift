import CSpeechwarp

/// What a score in 0 to 1 measures.
public enum TrainerMeasure: Int32, CaseIterable {
    /// Share of the words said back correctly from a sentence heard once.
    case intelligibility = 0
    /// Share right on "was this sentence in what you just heard?" items (the sentence verification technique:
    /// originals and paraphrases against changed-meaning and unrelated sentences). Chance is 0.5.
    case verification = 1
    /// Verification items about a session's material, answered after a delay; see
    /// `ListenerTrainer.addRetention`. Not used for thresholds.
    case retention = 2
    /// The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. Subjective, so
    /// weighted less by default.
    case rating = 3
}

/// Session plans: how the rate moves during a session.
public enum TrainerPlan: Int32, CaseIterable {
    /// The threshold plus a margin, all session.
    case steady = 0
    /// Start below the threshold and step up to threshold plus margin.
    case ramp = 1
    /// Alternate periods above and below the threshold.
    case interval = 2
    /// Move up or down after each in-session check, to stay at the target.
    case tracking = 3
}

/// Tunable numbers of a `ListenerTrainer`, with their defaults.
public enum TrainerParam: Int32, CaseIterable {
    /// Share understood that defines the threshold: 0.75 (0.5 to 0.95).
    case target = 0
    /// Steady and ramp: aim this fraction above the threshold: 0.10.
    case margin = 1
    /// Ramp: start at this fraction of the target rate: 0.8.
    case rampStart = 2
    /// Ramp: step by this fraction of the target rate: 0.02.
    case rampStep = 3
    /// Ramp: minutes between steps: 2.
    case rampMinutes = 4
    /// Interval: this fraction above, then below, the threshold: 0.15.
    case intervalSpread = 5
    /// Interval: minutes in each period: 10.
    case intervalMinutes = 6
    /// Tracking: ln(rate) moves by gain x (score - target) per check: 0.4.
    case trackingGain = 7
    /// Plans: threshold gain a hour worth a whole unit of retention: 0.2, so 10 points of retention are worth 2%
    /// a hour.
    case retentionCost = 8
    /// Threshold test: most presentations: 40.
    case testMax = 9
    /// Threshold test: done when the 95% interval's high / low is below this: 1.25.
    case testPrecision = 10
}

/// Pure logic for training a listener to follow faster speech: no audio, no clock, no storage. The caller passes
/// plain numbers in (scores, rates, its own timestamps) and gets rates and plans out. Deterministic: two
/// trainers created with the same seed and given the same calls give the same answers, so a caller keeps its
/// own log of calls and replays it into a new trainer to restore state.
///
/// The unit of rate everywhere is syllables a second heard: the source's syllable rate
/// (`SpeechwarpStream.syllableRate` or `SyllableCounter.rate()`) times the speed. Times are seconds on any clock
/// the caller likes (Unix time, say), and only differences are used. NaN arguments are ignored. Values that are
/// NaN without data are returned as NaN.
///
/// The protocol: a threshold test (`testBegin`, `testRate`, `addMeasure`, `testEnd`), `sessionBegin`, listening
/// with a check every ten minutes or so (`addMeasure`), a second test, `sessionEnd`; retention items a day and a
/// week later (`addRetention`).
///
/// A trainer is not thread safe. Use it from one thread, or lock around it.
public final class ListenerTrainer {
    private let trainer: OpaquePointer

    /// Creates a trainer. `seed` seeds its random choices.
    public init(seed: UInt64 = 0) throws {
        guard let trainer = speechwarp_trainer_create(seed) else { throw SpeechwarpError.outOfMemory }
        self.trainer = trainer
    }

    deinit {
        speechwarp_trainer_destroy(trainer)
    }

    /// How much a measure of each kind counts, per item, against the others: intelligibility 0.5, verification 1,
    /// retention 1, rating 0.3. 0 ignores the kind; negative and NaN are ignored.
    public func setWeight(_ kind: TrainerMeasure, to weight: Double) {
        speechwarp_trainer_set_weight(trainer, kind.rawValue, weight)
    }

    /// The weight of a kind of measure; see `setWeight`.
    public func getWeight(_ kind: TrainerMeasure) -> Double {
        speechwarp_trainer_get_weight(trainer, kind.rawValue)
    }

    /// Sets a tunable number; out-of-range values are clamped and NaN is ignored.
    public func setParam(_ param: TrainerParam, to value: Double) {
        speechwarp_trainer_set_param(trainer, param.rawValue, value)
    }

    /// The value of a tunable number.
    public func getParam(_ param: TrainerParam) -> Double {
        speechwarp_trainer_get_param(trainer, param.rawValue)
    }

    /// Adds a score: `kind` (not `.retention`), `score` 0..1, from `items` items (for a sentence repeated back,
    /// the number of words scored; for verification, the number of questions; for a rating, 1), heard at `rate`
    /// syllables a second, at `time`. During a threshold test it updates the estimate; during a session it is an
    /// in-session check, and the tracking plan reacts to it.
    ///
    /// - Returns: false if an argument is invalid.
    @discardableResult
    public func addMeasure(_ kind: TrainerMeasure, score: Double, items: Double, rate: Double, time: Double) -> Bool {
        speechwarp_trainer_add_measure(trainer, kind.rawValue, score, items, rate, time) != 0
    }

    /// Starts a threshold test: an estimate of the rate understood `TrainerParam.target` (75%) of the time, by the
    /// psi method (a Bayesian posterior over the threshold and the slope of a logistic psychometric function in
    /// log rate). The prior is log-normal around `priorRate` or, if that is 0, around the last estimate, or
    /// failing that around 10 syllables a second, within 3 to 60.
    public func testBegin(priorRate: Double, time: Double) {
        speechwarp_trainer_test_begin(trainer, priorRate, time)
    }

    /// The rate to present next in the test.
    public func testRate() -> Double {
        speechwarp_trainer_test_rate(trainer)
    }

    /// True once the 95% interval is narrower than `TrainerParam.testPrecision` (at least 8 presentations) or
    /// `TrainerParam.testMax` presentations have been scored; false otherwise or if no test is running.
    public var testDone: Bool { speechwarp_trainer_test_done(trainer) != 0 }

    /// Finishes the test; the estimate becomes the current threshold.
    ///
    /// - Returns: The threshold (0 if no test was running).
    @discardableResult
    public func testEnd(time: Double) -> Double {
        speechwarp_trainer_test_end(trainer, time)
    }

    /// The current estimate (posterior median) of the running test, else of the last one finished; 0 if none.
    public var threshold: Double { speechwarp_trainer_threshold(trainer) }

    /// The low end of the 95% interval of `threshold`; 0 if none.
    public var thresholdLow: Double { speechwarp_trainer_threshold_low(trainer) }

    /// The high end of the 95% interval of `threshold`; 0 if none.
    public var thresholdHigh: Double { speechwarp_trainer_threshold_high(trainer) }

    /// Begins a session under `plan`, from the threshold at this moment.
    public func sessionBegin(_ plan: TrainerPlan, time: Double) {
        speechwarp_trainer_session_begin(trainer, plan.rawValue, time)
    }

    /// The rate to play at now, under the session's plan; 0 if no session.
    public func sessionRate(time: Double) -> Double {
        speechwarp_trainer_session_rate(trainer, time)
    }

    /// Ends the session after `listeningHours` of listening in it. It is recorded for comparing plans if a
    /// threshold test ended after it began.
    ///
    /// - Returns: The session's number (0, 1, ...) for `addRetention`, or -1.
    @discardableResult
    public func sessionEnd(listeningHours: Double, time: Double) -> Int {
        Int(speechwarp_trainer_session_end(trainer, listeningHours, time))
    }

    /// Adds retention for a recorded `session`: `score` 0..1 from `items` items, answered `delaySeconds` after it
    /// ended.
    ///
    /// - Returns: false if an argument is invalid.
    @discardableResult
    public func addRetention(session: Int, score: Double, items: Double, delaySeconds: Double, time: Double) -> Bool {
        guard let session = Int32(exactly: session) else { return false }
        return speechwarp_trainer_add_retention(trainer, session, score, items, delaySeconds, time) != 0
    }

    /// The plan to run next: a Thompson draw over a Bayesian model of each recorded session's threshold gain
    /// (advances the random source).
    public func nextPlan() -> TrainerPlan {
        TrainerPlan(rawValue: speechwarp_trainer_next_plan(trainer)) ?? .steady
    }

    /// A plan's estimated threshold gain a hour now, as a fraction (0.01 is 1% a hour).
    public func planEffect(_ plan: TrainerPlan) -> Double {
        speechwarp_trainer_plan_effect(trainer, plan.rawValue)
    }

    /// The standard deviation of `planEffect`.
    public func planEffectSd(_ plan: TrainerPlan) -> Double {
        speechwarp_trainer_plan_effect_sd(trainer, plan.rawValue)
    }

    /// A plan's retention; NaN without data.
    public func planRetention(_ plan: TrainerPlan) -> Double {
        speechwarp_trainer_plan_retention(trainer, plan.rawValue)
    }

    /// The standard deviation of `planRetention`; NaN without data.
    public func planRetentionSd(_ plan: TrainerPlan) -> Double {
        speechwarp_trainer_plan_retention_sd(trainer, plan.rawValue)
    }

    /// The number of sessions recorded under a plan.
    public func planSessions(_ plan: TrainerPlan) -> Int {
        Int(speechwarp_trainer_plan_sessions(trainer, plan.rawValue))
    }

    /// The probability that a plan is the best by utility, from a fixed number of draws (does not advance the
    /// random source).
    public func planBestProbability(_ plan: TrainerPlan) -> Double {
        speechwarp_trainer_plan_best_probability(trainer, plan.rawValue)
    }

    /// The trend: the hours of listening by which gains have halved (1000 stands for "not slowing").
    public var trend: Double { speechwarp_trainer_trend(trainer) }

    /// The uncertainty of `trend` as a standard deviation of ln H.
    public var trendSd: Double { speechwarp_trainer_trend_sd(trainer) }
}
