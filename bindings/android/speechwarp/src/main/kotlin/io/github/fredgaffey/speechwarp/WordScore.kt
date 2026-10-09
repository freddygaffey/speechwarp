package io.github.fredgaffey.speechwarp

import java.text.Normalizer

/**
 * How well a listener repeated a sentence back: the reference sentence and what was heard (typed, or from
 * speech-to-text) aligned word by word. `right + missed + wrong` is the reference's word count and
 * `right + wrong + extra` the heard one's.
 *
 * @property share Words right as a share of the reference's words, 0 to 1. With no words in the reference it is
 *   1 if nothing was heard either and 0 otherwise.
 * @property right Reference words heard as they are.
 * @property missed Reference words not heard at all.
 * @property wrong Reference words heard as another word.
 * @property extra Heard words that are not in the reference.
 */
data class WordScore(val share: Double, val right: Int, val missed: Int, val wrong: Int, val extra: Int) {
    companion object {
        init {
            System.loadLibrary("speechwarp_jni")
        }

        /**
         * Scores [heard] (what the listener said or typed) against [reference] (the sentence played).
         *
         * Both are put in Unicode form NFC, then split into words the same way: letters folded to lower case
         * (ASCII and the Latin letters), punctuation dropped, an apostrophe inside a word kept (' and U+2019
         * alike, so "Don't" matches "don’t"), and numbers left as digits ("3" and "three" differ). The two are
         * aligned by word-level edit distance; among the cheapest alignments the one with the most words right
         * is taken. The rules in full are at speechwarp_score_words in include/speechwarp.h.
         *
         * @throws OutOfMemoryError if the native code could not allocate its working space.
         */
        @JvmStatic
        fun scoreWords(reference: String, heard: String): WordScore {
            val counts = IntArray(4)
            val share = nativeScore(utf8(reference), utf8(heard), counts)
            if (share < 0) throw OutOfMemoryError("speechwarp could not allocate space to score the words")
            return WordScore(share, counts[0], counts[1], counts[2], counts[3])
        }

        private fun utf8(text: String) = Normalizer.normalize(text, Normalizer.Form.NFC).toByteArray(Charsets.UTF_8)

        @JvmStatic private external fun nativeScore(reference: ByteArray, heard: ByteArray, counts: IntArray): Double
    }
}
