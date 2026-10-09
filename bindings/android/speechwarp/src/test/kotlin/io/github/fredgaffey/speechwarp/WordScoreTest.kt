package io.github.fredgaffey.speechwarp

import org.junit.Assert.assertEquals
import org.junit.Test

/** Runs on this computer's JVM against a native library built for it; see build.gradle.kts. */
class WordScoreTest {
    @Test
    fun scoresWords() {
        assertEquals(WordScore(1.0, 6, 0, 0, 0), WordScore.scoreWords("The cat sat on the mat.", "\"the CAT, sat on the mat!\""))
        assertEquals(WordScore(0.5, 2, 0, 2, 1), WordScore.scoreWords("one two three four", "one too tree four five"))
        assertEquals(WordScore(0.5, 1, 1, 0, 1), WordScore.scoreWords("a b", "b a"))
        assertEquals(1.0, WordScore.scoreWords("Don't stop", "don’t stop").share, 0.0)
        assertEquals(1.0, WordScore.scoreWords("Café au lait", "CAFÉ au lait").share, 0.0)
        // Decomposed e plus acute accent matches the composed letter after normalisation.
        assertEquals(1.0, WordScore.scoreWords("café", "café").share, 0.0)
        // A character outside the Basic Multilingual Plane arrives intact.
        assertEquals(WordScore(1.0, 2, 0, 0, 0), WordScore.scoreWords("hi 😀x", "HI 😀x"))
        assertEquals(WordScore(1.0, 0, 0, 0, 0), WordScore.scoreWords("", ""))
        assertEquals(WordScore(0.0, 0, 0, 0, 1), WordScore.scoreWords("", "hello"))
        assertEquals(WordScore(0.0, 0, 2, 0, 0), WordScore.scoreWords("hello world", ""))
    }
}
