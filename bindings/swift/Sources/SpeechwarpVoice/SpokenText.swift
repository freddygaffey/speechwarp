#if canImport(AVFoundation) && canImport(NaturalLanguage)
import AVFoundation
import Foundation
import NaturalLanguage
import Speechwarp

/// A text read aloud by a system voice and sped up by speechwarp, rendered sentence by sentence a little ahead
/// of playback.
///
/// ```swift
/// let voice = SpeechVoice.eloquence(language: "en-US").first { $0.name == "Reed" }!
/// let book = try SpokenText(text, voice: voice)
/// book.speed = 5                                   // on top of the voice's own rate
/// book.configure { $0.setHeardPause(0.03, fromSpeed: 3) }
/// // on the audio thread:
/// let samples = book.read(maxFrames: 1024)         // mono, book.sampleRate
/// // for the display:
/// let at = book.characterPosition                  // UTF-16 offset into the text
/// ```
///
/// Rendering runs on the main thread's run loop (where the system delivers speech); everything else is safe to
/// call from any thread. `read` never waits: if the next sentence is not rendered yet it returns what there is,
/// and `isBuffering` says so.
public final class SpokenText: @unchecked Sendable {
    /// A sentence: its range in the text, in UTF-16 offsets (as `NSString` and most UI text APIs count).
    public struct Sentence: Equatable, Sendable {
        public let range: NSRange
        public let text: String
    }

    public let text: String
    public let voice: SpeechVoice
    public let sentences: [Sentence]
    /// The sample rate of what `read` returns. Mono.
    public let sampleRate: Int

    /// The system rate the voice speaks at (see `SpeechRate`). Applies to sentences rendered afterwards.
    public var voiceRate: Float {
        get { lock.locked { rate } }
        set { lock.locked { rate = newValue; cache.removeAll(); rendered = [:] }; scheduleRendering() }
    }

    /// speechwarp's speed on top of the voice's own rate: 2 plays twice as fast again.
    public var speed: Float {
        get { lock.locked { stream.speed } }
        set { lock.locked { stream.speed = newValue }; scheduleRendering() }
    }

    /// Seconds of listening to keep rendered ahead of what is being heard. Default 30.
    public var lookahead: TimeInterval {
        get { lock.locked { ahead } }
        set { lock.locked { ahead = max(1, newValue) }; scheduleRendering() }
    }

    /// The UTF-16 offset in the text of what is being heard now (approximate within a sentence).
    public var characterPosition: Int { lock.locked { position() } }

    /// The sentence being heard now.
    public var sentenceIndex: Int { lock.locked { sentenceAt(position()) } }

    /// The last `read` returned less than asked because the next sentence was still being rendered.
    public var isBuffering: Bool { lock.locked { buffering } }

    /// Everything has been read.
    public var isFinished: Bool { lock.locked { flushed && stream.framesAvailable == 0 } }

    /// The most recent rendering failure, if any. A sentence that fails is skipped.
    public var lastError: Error? { lock.locked { failure } }

    private let lock = NSLock()
    private let renderer: SpeechRenderer
    private let stream: SpeechwarpStream
    private var rate: Float
    private var ahead: TimeInterval = 30
    private var cache: [Int: [Float]] = [:]
    private var rendered: [Int: Bool] = [:] // false while rendering
    private var feeding = 0 // the sentence being written into the stream
    private var fed = 0 // frames of it written
    private var segments: [(inputStart: Int64, sentence: Int, frames: Int)] = []
    private var written: Int64 = 0 // frames written into the stream since the last seek
    private var flushed = false
    private var buffering = false
    private var failure: Error?
    private var renderingNow = false

    /// Splits `text` into sentences and starts rendering the first ones. `voiceRate` is the system rate (see
    /// `SpeechRate`); `speed` multiplies it.
    public init(_ text: String, voice: SpeechVoice, voiceRate: Float = SpeechRate.normal) throws {
        self.text = text
        self.voice = voice
        renderer = try SpeechRenderer(voice: voice)
        sentences = Self.split(text)
        rate = voiceRate
        let settings = AVSpeechSynthesisVoice(identifier: voice.identifier)?.audioFileSettings
        sampleRate = (settings?[AVSampleRateKey] as? NSNumber)?.intValue ?? 22050
        stream = try SpeechwarpStream(sampleRate: sampleRate, channels: 1)
        scheduleRendering()
    }

    /// Changes the speechwarp stream's other settings (nonlinear amount, heard pause, floor blend, rhythm ...)
    /// safely. Do not keep the stream.
    public func configure(_ body: (SpeechwarpStream) -> Void) {
        lock.locked { body(stream) }
    }

    /// Returns up to `maxFrames` mono samples to play next. Never waits.
    public func read(maxFrames: Int) -> [Float] {
        let out: [Float] = lock.locked {
            feed(until: maxFrames)
            let samples = stream.read(maxFrames: maxFrames)
            buffering = samples.count < maxFrames && !flushed
            return samples
        }
        scheduleRendering()
        return out
    }

    /// Continues from the start of the sentence containing UTF-16 offset `offset`.
    public func seek(toCharacter offset: Int) {
        lock.locked {
            feeding = sentenceAt(offset)
            fed = 0
            stream.reset()
            segments = []
            written = 0
            flushed = false
        }
        scheduleRendering()
    }

    // MARK: Feeding the stream (called with the lock held)

    private func feed(until wanted: Int) {
        while stream.framesAvailable < wanted && !flushed {
            if feeding >= sentences.count {
                try? stream.flush()
                flushed = true
                return
            }
            if rendered[feeding] == true, cache[feeding] == nil {
                feeding += 1 // failed or empty: skip it
                fed = 0
                continue
            }
            guard let audio = cache[feeding] else { return } // not rendered yet
            let take = min(4096, audio.count - fed)
            if take > 0 {
                audio[fed..<fed + take].withUnsafeBufferPointer { try? stream.write($0) }
                if segments.last?.sentence == feeding {
                    segments[segments.count - 1].frames += take
                } else {
                    segments.append((written, feeding, take))
                }
                written += Int64(take)
                fed += take
            }
            if fed >= audio.count {
                feeding += 1
                fed = 0
                cache = cache.filter { $0.key >= feeding - 2 } // keep a little behind for short seeks
                rendered = rendered.filter { $0.key >= feeding - 2 }
            }
        }
    }

    private func position() -> Int {
        let heard = stream.position
        guard let segment = segments.last(where: { $0.inputStart <= heard }) else {
            return sentences.indices.contains(feeding) ? sentences[feeding].range.location : text.utf16.count
        }
        let range = sentences[segment.sentence].range
        let total = max(1, cache[segment.sentence]?.count ?? segment.frames)
        let into = Double(heard - segment.inputStart) / Double(total)
        return range.location + min(range.length, Int(into * Double(range.length)))
    }

    private func sentenceAt(_ offset: Int) -> Int {
        sentences.lastIndex { $0.range.location <= offset } ?? 0
    }

    // MARK: Rendering ahead

    /// Renders the next sentence that is missing within the lookahead, one at a time, until the lookahead is
    /// covered.
    private func scheduleRendering() {
        let next: (index: Int, text: String, rate: Float)? = lock.locked {
            guard !renderingNow else { return nil }
            var seconds = 0.0
            var index = feeding
            let listeningSpeed = Double(max(stream.speed, 0.05))
            while index < sentences.count && seconds < ahead {
                if let audio = cache[index] {
                    seconds += Double(audio.count) / Double(sampleRate) / listeningSpeed
                } else if rendered[index] == nil {
                    rendered[index] = false
                    renderingNow = true
                    return (index, sentences[index].text, rate)
                }
                index += 1
            }
            return nil
        }
        guard let next else { return }
        Task { [weak self] in
            guard let self else { return }
            do {
                let speech = try await self.renderer.render(next.text, rate: next.rate)
                let samples = speech.sampleRate == self.sampleRate
                    ? speech.samples : Self.resample(speech.samples, from: speech.sampleRate, to: self.sampleRate)
                self.lock.locked {
                    if self.rate == next.rate && next.index >= self.feeding - 2 {
                        if !samples.isEmpty { self.cache[next.index] = samples }
                        self.rendered[next.index] = true
                    } else {
                        self.rendered[next.index] = nil // the rate changed meanwhile, or it is behind us
                    }
                }
            } catch {
                self.lock.locked { self.failure = error; self.rendered[next.index] = true }
            }
            self.lock.locked { self.renderingNow = false }
            self.scheduleRendering()
        }
    }

    // MARK: Helpers

    /// Sentences by the system's tokenizer; any longer than 400 characters are cut at the last comma, semicolon
    /// or space before that, so that rendering never stalls on one enormous sentence.
    static func split(_ text: String) -> [Sentence] {
        let tokenizer = NLTokenizer(unit: .sentence)
        tokenizer.string = text
        var result: [Sentence] = []
        tokenizer.enumerateTokens(in: text.startIndex..<text.endIndex) { range, _ in
            var piece = range
            while text.distance(from: piece.lowerBound, to: piece.upperBound) > 400 {
                let limit = text.index(piece.lowerBound, offsetBy: 400)
                let window = text[piece.lowerBound..<limit]
                let cut = window.lastIndex(where: { ",;:".contains($0) }).map { text.index(after: $0) }
                    ?? window.lastIndex(where: \.isWhitespace) ?? limit
                result.append(Sentence(range: NSRange(piece.lowerBound..<cut, in: text),
                                       text: String(text[piece.lowerBound..<cut])))
                piece = cut..<piece.upperBound
            }
            let words = text[piece]
            if !words.allSatisfy(\.isWhitespace) {
                result.append(Sentence(range: NSRange(piece, in: text), text: String(words)))
            }
            return true
        }
        return result
    }

    /// Linear interpolation, for a voice whose audio is not at the rate it declared.
    static func resample(_ samples: [Float], from source: Int, to target: Int) -> [Float] {
        guard source != target, !samples.isEmpty else { return samples }
        let count = Int(Double(samples.count) * Double(target) / Double(source))
        return (0..<count).map { i in
            let x = Double(i) * Double(source) / Double(target)
            let j = Int(x), f = Float(x - Double(j))
            return j + 1 < samples.count ? samples[j] * (1 - f) + samples[j + 1] * f : samples[samples.count - 1]
        }
    }
}

private extension NSLock {
    func locked<T>(_ body: () throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}
#endif
