using System;
using System.Text;

namespace Speechwarp;

/// <summary>
/// How well a listener repeated a sentence back: the reference sentence and what was heard (typed, or from
/// speech-to-text) aligned word by word.
/// </summary>
/// <param name="Share">Words right as a share of the reference's words, 0 to 1. With no words in the reference it
/// is 1 if nothing was heard either and 0 otherwise.</param>
/// <param name="Right">Reference words heard as they are.</param>
/// <param name="Missed">Reference words not heard at all.</param>
/// <param name="Wrong">Reference words heard as another word.</param>
/// <param name="Extra">Heard words that are not in the reference.</param>
/// <remarks>
/// <see cref="Right"/> + <see cref="Missed"/> + <see cref="Wrong"/> is the reference's word count, and
/// <see cref="Right"/> + <see cref="Wrong"/> + <see cref="Extra"/> the heard one's.
/// </remarks>
public sealed record WordScore(double Share, int Right, int Missed, int Wrong, int Extra)
{
    /// <summary>
    /// Scores <paramref name="heard"/> against <paramref name="reference"/>. Both are put in Unicode form C, then
    /// split into words the same way: letters folded to lower case (ASCII and the Latin letters), punctuation
    /// dropped, an apostrophe inside a word kept (' and ’ alike, so "Don't" matches "don’t"), and numbers left
    /// as digits ("3" and "three" differ). The two are aligned by word-level edit distance; among the cheapest
    /// alignments the one with the most words right is taken. The rules in full are in include/speechwarp.h,
    /// speechwarp_score_words.
    /// </summary>
    /// <param name="reference">The sentence that was played.</param>
    /// <param name="heard">What the listener said or typed.</param>
    /// <returns>The share right and the four counts.</returns>
    /// <exception cref="ArgumentNullException">Either string is null.</exception>
    /// <exception cref="OutOfMemoryException">The native code could not allocate its working space.</exception>
    public static unsafe WordScore Of(string reference, string heard)
    {
        ArgumentNullException.ThrowIfNull(reference);
        ArgumentNullException.ThrowIfNull(heard);
        int* counts = stackalloc int[4];
        double share = Native.speechwarp_score_words(reference.Normalize(NormalizationForm.FormC),
            heard.Normalize(NormalizationForm.FormC), counts);
        if (share < 0)
            throw new OutOfMemoryException("speechwarp could not allocate space to score the words.");
        return new WordScore(share, counts[0], counts[1], counts[2], counts[3]);
    }
}
