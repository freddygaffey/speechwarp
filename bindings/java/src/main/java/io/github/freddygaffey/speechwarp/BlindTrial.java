package io.github.freddygaffey.speechwarp;

import java.util.Objects;

/** One blind A/B trial to run: the setting to compare and its two values in the order to play them. */
public final class BlindTrial {
    private final int setting;
    private final double first;
    private final double second;

    public BlindTrial(int setting, double first, double second) {
        this.setting = setting;
        this.first = first;
        this.second = second;
    }

    /** The setting's number. */
    public int setting() {
        return setting;
    }

    /** The value to play first. */
    public double first() {
        return first;
    }

    /** The value to play second. */
    public double second() {
        return second;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof BlindTrial)) {
            return false;
        }
        BlindTrial that = (BlindTrial) other;
        return setting == that.setting && Double.compare(first, that.first) == 0
                && Double.compare(second, that.second) == 0;
    }

    @Override
    public int hashCode() {
        return Objects.hash(setting, first, second);
    }

    @Override
    public String toString() {
        return "BlindTrial[setting=" + setting + ", first=" + first + ", second=" + second + "]";
    }
}
