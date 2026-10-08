package io.github.freddygaffey.speechwarp;

/** What a score in 0 to 1 measures. */
public enum TrainerMeasure {
    /** Share of the words said back correctly from a sentence heard once. */
    INTELLIGIBILITY(0),
    /**
     * Share right on "was this sentence in what you just heard?" items (the sentence verification technique:
     * originals and paraphrases against changed-meaning and unrelated sentences). Chance is 0.5.
     */
    VERIFICATION(1),
    /**
     * Verification items about a session's material, answered after a delay; see {@link
     * ListenerTrainer#addRetention}.
     */
    RETENTION(2),
    /**
     * The listener's own "how well did you follow?", 1 to 5 scaled to 0..1 as (r - 1) / 4. Weighted less by
     * default.
     */
    RATING(3);

    private final int value;

    TrainerMeasure(int value) {
        this.value = value;
    }

    /** The value this has in the C library. */
    public int value() {
        return value;
    }
}
