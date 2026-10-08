package io.github.freddygaffey.speechwarp;

/** Session plans: how the rate moves during a session. */
public enum TrainerPlan {
    /** The threshold plus a margin, all session. */
    STEADY(0),
    /** Start below the threshold and step up to threshold plus margin. */
    RAMP(1),
    /** Alternate periods above and below the threshold. */
    INTERVAL(2),
    /** Move up or down after each in-session check, to stay at the target. */
    TRACKING(3);

    private final int value;

    TrainerPlan(int value) {
        this.value = value;
    }

    /** The value this has in the C library. */
    public int value() {
        return value;
    }

    static TrainerPlan of(int value) {
        for (TrainerPlan plan : values()) {
            if (plan.value == value) {
                return plan;
            }
        }
        throw new IllegalStateException("unknown plan " + value);
    }
}
