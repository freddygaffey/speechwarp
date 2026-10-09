// The parts of Apple speech recognition that are plain logic, kept apart so that they can be tested without a
// recogniser: grouping timed words into segments, and choosing where to cut a long stream into requests.
import Foundation
import Speechwarp

enum TranscriptAssembly {
    /// Groups timed words into segments, ending one after a sentence's closing punctuation, at a pause of
    /// `pauseGap` seconds or more, or when it would grow longer than `maxSeconds`. Words are offset by
    /// `offset` seconds. A segment keeps its words only when `keepWords` is true.
    static func segments(from words: [TranscriptWord], offset: Double = 0, pauseGap: Double = 0.7,
                         maxSeconds: Double = 30, keepWords: Bool = true) -> [TranscriptSegment] {
        var segments: [TranscriptSegment] = []
        var current: [TranscriptWord] = []

        func close() {
            guard let first = current.first, let last = current.last else { return }
            let text = current.map(\.text).joined(separator: " ")
            segments.append(TranscriptSegment(text: text, start: first.start, end: last.end,
                                              words: keepWords ? current : []))
            current = []
        }

        for word in words {
            let text = word.text.trimmingCharacters(in: .whitespacesAndNewlines)
            if text.isEmpty { continue }
            let shifted = TranscriptWord(text: text, start: word.start + offset, end: word.end + offset,
                                         confidence: word.confidence)
            if let last = current.last, let first = current.first,
               shifted.start - last.end >= pauseGap || shifted.end - first.start > maxSeconds {
                close()
            }
            current.append(shifted)
            if endsSentence(text) { close() }
        }
        close()
        return segments
    }

    /// The segment moved later by `seconds`, words and all.
    static func offset(_ segment: TranscriptSegment, by seconds: Double) -> TranscriptSegment {
        if seconds == 0 { return segment }
        return TranscriptSegment(
            text: segment.text, start: segment.start + seconds, end: segment.end + seconds,
            words: segment.words.map {
                TranscriptWord(text: $0.text, start: $0.start + seconds, end: $0.end + seconds,
                               confidence: $0.confidence)
            })
    }

    /// Whether a word ends a sentence: a full stop, question or exclamation mark, possibly inside quotes.
    static func endsSentence(_ word: String) -> Bool {
        let trimmed = word.trimmingCharacters(in: CharacterSet(charactersIn: "\"'”’)]»"))
        guard let last = trimmed.last else { return false }
        return ".?!…。？！".contains(last)
    }
}

/// Splits a stream of audio into requests of a bounded length, cutting in a pause where it can.
///
/// Apple's older recogniser (SFSpeechRecognizer) is made for utterances, not hours, so a long input is given to
/// it as a series of requests. Audio passes through in blocks of 100 ms; once a request is `minSeconds` long,
/// the first quiet block ends it, cut in the middle of the block; at `maxSeconds` it ends wherever it is.
struct ChunkCutter {
    /// What to do with the audio, in order.
    enum Piece: Equatable {
        /// Samples for the current request.
        case audio([Float])
        /// End the current request; what follows starts a new one.
        case cut
    }

    let blockFrames: Int
    let minFrames: Int
    let maxFrames: Int
    /// Frames in the current request so far.
    private(set) var requestFrames = 0
    private var block: [Float] = []
    private var peak: Float = 0

    init(sampleRate: Int, minSeconds: Double = 20, maxSeconds: Double = 50) {
        blockFrames = max(1, sampleRate / 10)
        minFrames = Int(Double(sampleRate) * minSeconds)
        maxFrames = max(Int(Double(sampleRate) * maxSeconds), minFrames + blockFrames)
    }

    /// Takes samples and returns what can be passed on now. Up to one block is held back.
    mutating func push(_ samples: [Float]) -> [Piece] {
        var pieces: [Piece] = []
        var index = 0
        while index < samples.count {
            let take = min(blockFrames - block.count, samples.count - index)
            block.append(contentsOf: samples[index..<index + take])
            index += take
            if block.count == blockFrames {
                emit(block, into: &pieces)
                block.removeAll(keepingCapacity: true)
            }
        }
        return pieces
    }

    /// Passes on whatever is held back, at the end of the input.
    mutating func flush() -> [Piece] {
        guard !block.isEmpty else { return [] }
        var pieces: [Piece] = []
        if requestFrames + block.count > maxFrames { pieces.append(.cut); requestFrames = 0 }
        pieces.append(.audio(block))
        requestFrames += block.count
        block.removeAll()
        return pieces
    }

    private mutating func emit(_ block: [Float], into pieces: inout [Piece]) {
        var sum: Float = 0
        for sample in block { sum += sample * sample }
        let rms = (sum / Float(block.count)).squareRoot()
        // The loudness to compare with falls by about a factor of e every 20 s, so a loud start does not make
        // everything after it count as quiet.
        peak = max(rms, peak * 0.995)
        let quiet = rms < 0.001 || rms < peak * 0.03

        if requestFrames >= minFrames && quiet {
            let half = block.count / 2
            pieces.append(.audio(Array(block[..<half])))
            pieces.append(.cut)
            pieces.append(.audio(Array(block[half...])))
            requestFrames = block.count - half
        } else if requestFrames + block.count > maxFrames {
            pieces.append(.cut)
            pieces.append(.audio(block))
            requestFrames = block.count
        } else {
            pieces.append(.audio(block))
            requestFrames += block.count
        }
    }
}
