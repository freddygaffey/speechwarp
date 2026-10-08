package io.github.freddygaffey.speechwarp;

/** Tunable numbers of a {@link ListenerTrainer}, with their defaults. */
public enum TrainerParam {
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
    /**
     * Plans: threshold gain a hour worth a whole unit of retention: 0.2, so 10 points of retention are worth 2%
     * a hour.
     */
    RETENTION_COST(8),
    /** Threshold test: most presentations: 40. */
    TEST_MAX(9),
    /** Threshold test: done when the 95% interval's high / low is below this: 1.25. */
    TEST_PRECISION(10);

    private final int value;

    TrainerParam(int value) {
        this.value = value;
    }

    /** The value this has in the C library. */
    public int value() {
        return value;
    }
}
