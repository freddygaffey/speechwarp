import CSpeechwarp

/// One blind A/B trial to run: the `setting` to compare and its two values in the order to play them.
public struct BlindTrial: Equatable {
    public var setting: Int
    public var first: Double
    public var second: Double

    public init(setting: Int, first: Double, second: Double) {
        self.setting = setting
        self.first = first
        self.second = second
    }
}

/// Designs the listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
/// values, in what order, and how results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...). The
/// caller names the settings; here they are numbers (0, 1, ... in the order added). Deterministic for a given
/// seed.
///
/// Not thread safe. Use it from one thread, or lock around it.
public final class BlindTrials {
    private let trials: OpaquePointer

    /// Creates trials. `seed` seeds the random choices.
    public init(seed: UInt64 = 0) throws {
        guard let trials = speechwarp_trials_create(seed) else { throw SpeechwarpError.outOfMemory }
        self.trials = trials
    }

    deinit {
        speechwarp_trials_destroy(trials)
    }

    /// Adds a setting.
    ///
    /// - Returns: Its number (0, 1, ...), or -1.
    @discardableResult
    public func addSetting() -> Int {
        Int(speechwarp_trials_add_setting(trials))
    }

    /// Adds a value to compare to a setting.
    ///
    /// - Returns: Its number within the setting, or -1 (duplicate, bad setting).
    @discardableResult
    public func addValue(setting: Int, value: Double) -> Int {
        guard let setting = Int32(exactly: setting) else { return -1 }
        return Int(speechwarp_trials_add_value(trials, setting, value))
    }

    /// Leaves a setting out of `next(speed:)` while false (say, when its method is not available). Default true.
    public func setAvailable(setting: Int, _ available: Bool) {
        guard let setting = Int32(exactly: setting) else { return }
        speechwarp_trials_set_available(trials, setting, available ? 1 : 0)
    }

    /// Records a trial at `speed`: the two values in the order heard, each one's score 0..1, and `preferred`: -1
    /// the first, 1 the second, 0 neither. Values must be ones added.
    ///
    /// - Returns: false if an argument is invalid.
    @discardableResult
    public func add(
        setting: Int, speed: Double, firstValue: Double, secondValue: Double, firstScore: Double,
        secondScore: Double, preferred: Int
    ) -> Bool {
        guard let setting = Int32(exactly: setting), let preferred = Int32(exactly: preferred) else { return false }
        return speechwarp_trials_add(
            trials, setting, speed, firstValue, secondValue, firstScore, secondScore, preferred) != 0
    }

    /// Chooses the next trial at `speed`: the available setting with the fewest trials in its band (ties at
    /// random), its pair of values compared least (ties at random), in random order. Nil if no setting has two
    /// values.
    public func next(speed: Double) -> BlindTrial? {
        let setting = speechwarp_trials_next(trials, speed)
        guard setting >= 0 else { return nil }
        return BlindTrial(
            setting: Int(setting), first: speechwarp_trials_next_first(trials),
            second: speechwarp_trials_next_second(trials))
    }

    /// Comparisons a value has won in the band of `speed`; `value` is its number within the setting.
    public func won(setting: Int, speed: Double, value: Int) -> Int {
        count(speechwarp_trials_won, setting, speed, value)
    }

    /// Comparisons a value has lost in the band of `speed`.
    public func lost(setting: Int, speed: Double, value: Int) -> Int {
        count(speechwarp_trials_lost, setting, speed, value)
    }

    /// Comparisons a value has tied in the band of `speed`.
    public func tied(setting: Int, speed: Double, value: Int) -> Int {
        count(speechwarp_trials_tied, setting, speed, value)
    }

    /// Trials a value was heard in, in the band of `speed`.
    public func heard(setting: Int, speed: Double, value: Int) -> Int {
        count(speechwarp_trials_heard, setting, speed, value)
    }

    /// A value's mean score in the band of `speed`; nil if never heard.
    public func meanScore(setting: Int, speed: Double, value: Int) -> Double? {
        guard let setting = Int32(exactly: setting), let value = Int32(exactly: value) else { return nil }
        let score = speechwarp_trials_mean_score(trials, setting, speed, value)
        return score.isNaN ? nil : score
    }

    /// The value (its number within the setting) with a reliable win in the band of `speed`, or nil. A value wins
    /// when it has been heard in at least 5 trials, has met every other value in at least 3, and against each
    /// the Bayes factor for "preferred" over "no preference" is at least 1 / (1 - confidence): 20 at the default
    /// 0.95. However often this is asked, the chance of ever naming a winner between two values that are really
    /// alike is at most 1 - confidence each way.
    public func winner(setting: Int, speed: Double) -> Int? {
        guard let setting = Int32(exactly: setting) else { return nil }
        let value = speechwarp_trials_winner(trials, setting, speed)
        return value < 0 ? nil : Int(value)
    }

    /// Sets the confidence a winner needs; see `winner(setting:speed:)`.
    public func setConfidence(_ confidence: Double) {
        speechwarp_trials_set_confidence(trials, confidence)
    }

    private func count(
        _ function: (OpaquePointer?, Int32, Double, Int32) -> Int32, _ setting: Int, _ speed: Double, _ value: Int
    ) -> Int {
        guard let setting = Int32(exactly: setting), let value = Int32(exactly: value) else { return 0 }
        return Int(function(trials, setting, speed, value))
    }
}
