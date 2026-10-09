package io.github.fredgaffey.speechwarp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

public class WordScoreTest {
    @Test
    public void scoresWords() {
        assertEquals(new WordScore(1, 6, 0, 0, 0),
                WordScore.scoreWords("The cat sat on the mat.", "\"the CAT, sat on the mat!\""));
        assertEquals(new WordScore(0.5, 2, 0, 2, 1), WordScore.scoreWords("one two three four", "one too tree four five"));
        assertEquals(new WordScore(0.5, 1, 1, 0, 1), WordScore.scoreWords("a b", "b a"));
        assertEquals(1.0, WordScore.scoreWords("Don't stop", "don’t stop").share(), 0);
        assertEquals(1.0, WordScore.scoreWords("Café au lait", "CAFÉ au lait").share(), 0);
        // Decomposed e plus acute accent matches the composed letter after normalisation.
        assertEquals(1.0, WordScore.scoreWords("café", "café").share(), 0);
        // A character outside the Basic Multilingual Plane arrives intact.
        assertEquals(new WordScore(1, 2, 0, 0, 0), WordScore.scoreWords("hi 😀x", "HI 😀x"));
        assertEquals(new WordScore(1, 0, 0, 0, 0), WordScore.scoreWords("", ""));
        assertEquals(new WordScore(0, 0, 0, 0, 1), WordScore.scoreWords("", "hello"));
        assertEquals(new WordScore(0, 0, 2, 0, 0), WordScore.scoreWords("hello world", ""));
        assertThrows(NullPointerException.class, () -> WordScore.scoreWords(null, "x"));
    }
}
