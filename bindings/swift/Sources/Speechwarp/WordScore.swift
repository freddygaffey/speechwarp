import CSpeechwarp
import Foundation

/// How well a listener repeated a sentence back: the reference sentence and what was heard (typed, or from
/// speech-to-text) aligned word by word. `right + missed + wrong` is the reference's word count and
/// `right + wrong + extra` the heard one's.
public struct WordScore: Equatable {
    /// Words right as a share of the reference's words, 0 to 1. With no words in the reference it is 1 if nothing
    /// was heard either and 0 otherwise.
    public var share: Double
    /// Reference words heard as they are.
    public var right: Int
    /// Reference words not heard at all.
    public var missed: Int
    /// Reference words heard as another word.
    public var wrong: Int
    /// Heard words that are not in the reference.
    public var extra: Int

    public init(share: Double, right: Int, missed: Int, wrong: Int, extra: Int) {
        self.share = share
        self.right = right
        self.missed = missed
        self.wrong = wrong
        self.extra = extra
    }
}

/// Scores `heard` (what the listener said or typed) against `reference` (the sentence played).
///
/// Both are put in Unicode form C, then split into words the same way: letters folded to lower case (ASCII and
/// the Latin letters), punctuation dropped, an apostrophe inside a word kept (' and ’ alike, so "Don't" matches
/// "don’t"), and numbers left as digits ("3" and "three" differ). The two are aligned by word-level edit
/// distance; among the cheapest alignments the one with the most words right is taken. The rules in full are at
/// `speechwarp_score_words` in include/speechwarp.h.
///
/// - Throws: `SpeechwarpError.outOfMemory` if the native code could not allocate its working space.
public func scoreWords(reference: String, heard: String) throws -> WordScore {
    var counts = [Int32](repeating: 0, count: 4)
    let share = reference.precomposedStringWithCanonicalMapping.withCString { referenceText in
        heard.precomposedStringWithCanonicalMapping.withCString { heardText in
            speechwarp_score_words(referenceText, heardText, &counts)
        }
    }
    guard share >= 0 else { throw SpeechwarpError.outOfMemory }
    return WordScore(share: share, right: Int(counts[0]), missed: Int(counts[1]), wrong: Int(counts[2]),
                     extra: Int(counts[3]))
}
