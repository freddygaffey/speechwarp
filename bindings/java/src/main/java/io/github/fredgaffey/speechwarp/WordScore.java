package io.github.fredgaffey.speechwarp;

import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.Objects;

/**
 * How well a listener repeated a sentence back: the reference sentence and what was heard (typed, or from
 * speech-to-text) aligned word by word. {@code right + missed + wrong} is the reference's word count and
 * {@code right + wrong + extra} the heard one's.
 */
public final class WordScore {
    static {
        NativeLibrary.ensureLoaded();
    }

    private final double share;
    private final int right;
    private final int missed;
    private final int wrong;
    private final int extra;

    public WordScore(double share, int right, int missed, int wrong, int extra) {
        this.share = share;
        this.right = right;
        this.missed = missed;
        this.wrong = wrong;
        this.extra = extra;
    }

    /**
     * Scores {@code heard} (what the listener said or typed) against {@code reference} (the sentence played).
     *
     * <p>Both are put in Unicode form NFC, then split into words the same way: letters folded to lower case (ASCII
     * and the Latin letters), punctuation dropped, an apostrophe inside a word kept (' and U+2019 alike, so
     * "Don't" matches "don’t"), and numbers left as digits ("3" and "three" differ). The two are aligned by
     * word-level edit distance; among the cheapest alignments the one with the most words right is taken. The
     * rules in full are at speechwarp_score_words in include/speechwarp.h.
     *
     * @throws NullPointerException if either string is null
     * @throws OutOfMemoryError if the native code could not allocate its working space
     */
    public static WordScore scoreWords(String reference, String heard) {
        Objects.requireNonNull(reference, "reference");
        Objects.requireNonNull(heard, "heard");
        int[] counts = new int[4];
        double share = nativeScore(utf8(reference), utf8(heard), counts);
        if (share < 0) {
            throw new OutOfMemoryError("speechwarp could not allocate space to score the words");
        }
        return new WordScore(share, counts[0], counts[1], counts[2], counts[3]);
    }

    private static byte[] utf8(String text) {
        return Normalizer.normalize(text, Normalizer.Form.NFC).getBytes(StandardCharsets.UTF_8);
    }

    /**
     * Words right as a share of the reference's words, 0 to 1. With no words in the reference it is 1 if nothing
     * was heard either and 0 otherwise.
     */
    public double share() {
        return share;
    }

    /** Reference words heard as they are. */
    public int right() {
        return right;
    }

    /** Reference words not heard at all. */
    public int missed() {
        return missed;
    }

    /** Reference words heard as another word. */
    public int wrong() {
        return wrong;
    }

    /** Heard words that are not in the reference. */
    public int extra() {
        return extra;
    }

    @Override
    public boolean equals(Object other) {
        if (!(other instanceof WordScore)) {
            return false;
        }
        WordScore that = (WordScore) other;
        return Double.compare(share, that.share) == 0 && right == that.right && missed == that.missed
                && wrong == that.wrong && extra == that.extra;
    }

    @Override
    public int hashCode() {
        return Objects.hash(share, right, missed, wrong, extra);
    }

    @Override
    public String toString() {
        return "WordScore[share=" + share + ", right=" + right + ", missed=" + missed + ", wrong=" + wrong
                + ", extra=" + extra + "]";
    }

    private static native double nativeScore(byte[] reference, byte[] heard, int[] counts);
}
