#if canImport(Speech) && (os(iOS) || os(macOS))
import AVFoundation
import CoreMedia
import Foundation
import Speech
import Speechwarp

/// The state every Apple session keeps for its caller: finished segments not yet taken, the partial guess and
/// the counts. Thread safe.
class SessionOutput: @unchecked Sendable {
    let lock = NSLock()
    let sampleRate: Int
    private var ready: [TranscriptSegment] = []
    private var guess: TranscriptSegment?
    private var framesWritten = 0
    private var recognised = 0.0
    private var handler: (@Sendable () -> Void)?

    init(sampleRate: Int) {
        self.sampleRate = sampleRate
    }

    func takeSegments() -> [TranscriptSegment] {
        lock.synchronized {
            defer { ready = [] }
            return ready
        }
    }

    var partial: TranscriptSegment? { lock.synchronized { guess } }
    var secondsWritten: Double { lock.synchronized { Double(framesWritten) / Double(sampleRate) } }
    var secondsRecognised: Double { lock.synchronized { min(recognised, Double(framesWritten) / Double(sampleRate)) } }

    var onSegmentsReady: (@Sendable () -> Void)? {
        get { lock.synchronized { handler } }
        set { lock.synchronized { handler = newValue } }
    }

    func countWritten(_ frames: Int) {
        lock.synchronized { framesWritten += frames }
    }

    /// Adds finished segments, moves the recognised time on to `through` and clears the guess if they cover it.
    func add(_ segments: [TranscriptSegment], through: Double) {
        let handler: (@Sendable () -> Void)? = lock.synchronized {
            ready.append(contentsOf: segments)
            recognised = max(recognised, through)
            if let current = guess, current.end <= through { guess = nil }
            return segments.isEmpty ? nil : self.handler
        }
        handler?()
    }

    func setPartial(_ segment: TranscriptSegment?) {
        lock.synchronized { guess = segment }
    }

    /// Everything not yet taken, as a transcript, at the end.
    func finishAll() -> Transcript {
        lock.synchronized {
            guess = nil
            recognised = Double(framesWritten) / Double(sampleRate)
        }
        return Transcript(segments: takeSegments())
    }
}

// MARK: - SpeechAnalyzer (iOS 26, macOS 26)

/// What the analyser's two transcribers' results have in common.
@available(macOS 26, iOS 26, *)
protocol AnalyzerTextResult {
    var text: AttributedString { get }
    var range: CMTimeRange { get }
    var isFinal: Bool { get }
}

@available(macOS 26, iOS 26, *)
extension SpeechTranscriber.Result: AnalyzerTextResult {}

@available(macOS 26, iOS 26, *)
extension DictationTranscriber.Result: AnalyzerTextResult {}

/// A session on `SpeechAnalyzer`. Samples written are queued; one task converts them to the analyser's format
/// and feeds it, another collects results.
@available(macOS 26, iOS 26, *)
final class AnalyzerSession: SessionOutput, TranscriptionSession, @unchecked Sendable {
    private let input: AsyncStream<[Float]>.Continuation
    private var work: Task<Void, Error>!
    private var ended = false

    /// The modules for a session: one transcriber, set up for `options`.
    static func modules(_ module: AppleTranscriber.AnalyzerModule, locale: Locale,
                        options: TranscriptionOptions) -> [any SpeechModule] {
        switch module {
        case .transcriber:
            var reporting: Set<SpeechTranscriber.ReportingOption> = [.volatileResults]
            if options.preset == .fast { reporting.insert(.fastResults) }
            return [SpeechTranscriber(locale: locale, transcriptionOptions: [], reportingOptions: reporting,
                                      attributeOptions: [.audioTimeRange, .transcriptionConfidence])]
        case .dictation:
            var reporting: Set<DictationTranscriber.ReportingOption> = [.volatileResults]
            if options.preset == .fast { reporting.insert(.frequentFinalization) }
            return [DictationTranscriber(locale: locale, contentHints: [], transcriptionOptions: [.punctuation],
                                         reportingOptions: reporting,
                                         attributeOptions: [.audioTimeRange, .transcriptionConfidence])]
        }
    }

    init(module: AppleTranscriber.AnalyzerModule, locale: Locale, sampleRate: Int, options: TranscriptionOptions) {
        let (stream, input) = AsyncStream<[Float]>.makeStream()
        self.input = input
        super.init(sampleRate: sampleRate)
        let modules = Self.modules(module, locale: locale, options: options)
        work = Task {
            try await self.run(modules: modules, samples: stream, options: options)
        }
    }

    func write(_ samples: [Float]) {
        guard !samples.isEmpty else { return }
        let accepted: Bool = lock.synchronized { !ended }
        guard accepted else { return }
        countWritten(samples.count)
        input.yield(samples)
    }

    func finish() async throws -> Transcript {
        lock.synchronized { ended = true }
        input.finish()
        do {
            try await work.value
        } catch let error as AppleTranscriberError {
            throw error
        } catch {
            throw AppleTranscriberError.recognitionFailed(error.localizedDescription)
        }
        return finishAll()
    }

    private func run(modules: [any SpeechModule], samples: AsyncStream<[Float]>,
                     options: TranscriptionOptions) async throws {
        let analyzer = SpeechAnalyzer(modules: modules)
        if !options.hints.isEmpty {
            let context = AnalysisContext()
            context.contextualStrings[.general] = options.hints
            try await analyzer.setContext(context)
        }
        let format = await SpeechAnalyzer.bestAvailableAudioFormat(compatibleWith: modules)
            ?? AudioSamples.monoFormat(sampleRate: sampleRate)
        try await analyzer.prepareToAnalyze(in: format)
        guard let converter = AudioStreamConverter(from: AudioSamples.monoFormat(sampleRate: sampleRate), to: format)
        else { throw AppleTranscriberError.recognitionFailed("Cannot convert audio to \(format)") }

        let words = options.wordTimestamps
        let results: Task<Void, Error>
        if let transcriber = modules.first as? SpeechTranscriber {
            results = Task { try await self.collect(transcriber.results, words: words) }
        } else if let dictation = modules.first as? DictationTranscriber {
            results = Task { try await self.collect(dictation.results, words: words) }
        } else {
            preconditionFailure("unknown module")
        }

        let (inputs, feed) = AsyncStream<AnalyzerInput>.makeStream()
        try await analyzer.start(inputSequence: inputs)
        for await chunk in samples {
            if let buffer = AudioSamples.buffer(chunk, sampleRate: sampleRate), let converted = converter.convert(buffer) {
                feed.yield(AnalyzerInput(buffer: converted))
            }
        }
        if let tail = converter.flush() { feed.yield(AnalyzerInput(buffer: tail)) }
        feed.finish()
        if secondsWritten > 0 {
            try await analyzer.finalizeAndFinishThroughEndOfInput()
            try await results.value
        } else {
            // Nothing to recognise: the analyser ends its results with a cancellation, which is no failure.
            await analyzer.cancelAndFinishNow()
            results.cancel()
            _ = try? await results.value
        }
    }

    private func collect<Results: AsyncSequence>(_ results: Results, words: Bool) async throws
        where Results.Element: AnalyzerTextResult {
        for try await result in results {
            let segment = Self.segment(result, words: words)
            if result.isFinal {
                add(segment.map { [$0] } ?? [], through: result.range.end.seconds)
            } else {
                setPartial(segment)
            }
        }
    }

    /// The result as a segment, or nil if it holds no words.
    static func segment(_ result: some AnalyzerTextResult, words keepWords: Bool) -> TranscriptSegment? {
        let text = String(result.text.characters).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        var words: [TranscriptWord] = []
        if keepWords {
            for run in result.text.runs {
                guard let range = run.audioTimeRange else { continue }
                let word = String(result.text[run.range].characters).trimmingCharacters(in: .whitespacesAndNewlines)
                if word.isEmpty { continue }
                words.append(TranscriptWord(text: word, start: range.start.seconds, end: range.end.seconds,
                                            confidence: run.transcriptionConfidence ?? .nan))
            }
        }
        let start = words.first?.start ?? result.range.start.seconds
        let end = words.last?.end ?? result.range.end.seconds
        return TranscriptSegment(text: text, start: start, end: end, words: words)
    }
}

// MARK: - SFSpeechRecognizer

/// A session on `SFSpeechRecognizer`, on the device. The audio is cut into requests (see `ChunkCutter`) that
/// are recognised one after another; the one being filled gets the audio as it arrives, so a live microphone
/// sees partial results.
final class RecognizerSession: SessionOutput, TranscriptionSession, @unchecked Sendable {
    /// A request waiting to start: its audio so far, from frame `start` of the session.
    private final class Chunk {
        let start: Int
        var samples: [Float] = []
        var frames = 0
        var ended = false
        var request: SFSpeechAudioBufferRecognitionRequest?
        var task: SFSpeechRecognitionTask?
        var done = false

        init(start: Int) {
            self.start = start
        }
    }

    private let recognizer: SFSpeechRecognizer
    private let options: TranscriptionOptions
    private let queue = DispatchQueue(label: "speechwarp.recogniser-session")
    // Everything below is used only on `queue`.
    private var cutter: ChunkCutter
    private var waiting: [Chunk] = []
    private var active: Chunk?
    private var framesCut = 0
    private var inputEnded = false
    private var finished: CheckedContinuation<Void, Never>?
    private var failure: String?

    init(recognizer: SFSpeechRecognizer, sampleRate: Int, options: TranscriptionOptions) {
        self.recognizer = recognizer
        self.options = options
        cutter = ChunkCutter(sampleRate: sampleRate)
        super.init(sampleRate: sampleRate)
        let callbacks = OperationQueue()
        callbacks.maxConcurrentOperationCount = 1
        recognizer.queue = callbacks
    }

    func write(_ samples: [Float]) {
        guard !samples.isEmpty else { return }
        countWritten(samples.count)
        queue.async {
            guard !self.inputEnded else { return }
            for piece in self.cutter.push(samples) { self.handle(piece) }
        }
    }

    func finish() async throws -> Transcript {
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            queue.async {
                if !self.inputEnded {
                    for piece in self.cutter.flush() { self.handle(piece) }
                    self.handle(.cut)
                    self.inputEnded = true
                }
                self.finished = continuation
                self.startNext()
                self.checkFinished()
            }
        }
        if let failure = queue.sync(execute: { self.failure }) {
            throw AppleTranscriberError.recognitionFailed(failure)
        }
        return finishAll()
    }

    /// The request being filled: the last waiting one, or the active one, if it has not been ended.
    private var filling: Chunk? {
        if let last = waiting.last { return last.ended ? nil : last }
        if let active, !active.ended { return active }
        return nil
    }

    private func handle(_ piece: ChunkCutter.Piece) {
        switch piece {
        case .audio(let samples):
            let chunk: Chunk
            if let filling {
                chunk = filling
            } else {
                chunk = Chunk(start: framesCut)
                waiting.append(chunk)
            }
            chunk.frames += samples.count
            if let request = chunk.request {
                if let buffer = AudioSamples.buffer(samples, sampleRate: sampleRate) { request.append(buffer) }
            } else {
                chunk.samples.append(contentsOf: samples)
            }
            startNext()
        case .cut:
            guard let chunk = filling else { return }
            chunk.ended = true
            chunk.request?.endAudio()
            framesCut = chunk.start + chunk.frames
        }
    }

    /// Starts the next waiting request if none is running.
    private func startNext() {
        guard active == nil, !waiting.isEmpty else { return }
        let chunk = waiting.removeFirst()
        active = chunk
        let request = SFSpeechAudioBufferRecognitionRequest()
        request.requiresOnDeviceRecognition = true
        request.shouldReportPartialResults = true
        request.taskHint = .dictation
        request.contextualStrings = options.hints
        if #available(macOS 13, iOS 16, *) { request.addsPunctuation = true }
        chunk.request = request
        if let buffer = AudioSamples.buffer(chunk.samples, sampleRate: sampleRate) { request.append(buffer) }
        chunk.samples = []
        if chunk.ended { request.endAudio() }
        chunk.task = recognizer.recognitionTask(with: request) { [weak self] result, error in
            guard let self else { return }
            self.queue.async { self.received(result, error, for: chunk) }
        }
    }

    private func received(_ result: SFSpeechRecognitionResult?, _ error: Error?, for chunk: Chunk) {
        guard !chunk.done else { return }
        let offset = Double(chunk.start) / Double(sampleRate)
        if let result, !result.isFinal, error == nil {
            let words = Self.words(result.bestTranscription)
            setPartial(TranscriptAssembly.segments(from: words, offset: offset, pauseGap: .infinity,
                                                   maxSeconds: .infinity, keepWords: options.wordTimestamps).first)
            return
        }
        chunk.done = true
        if !chunk.ended {
            // The recogniser stopped before the request was ended (an error): later audio starts a new one.
            chunk.ended = true
            framesCut = chunk.start + chunk.frames
        }
        chunk.task = nil
        chunk.request = nil
        var segments: [TranscriptSegment] = []
        if let result, result.isFinal {
            // The recogniser can place the last word's end a little past the audio; keep it inside.
            let length = Double(chunk.frames) / Double(sampleRate)
            let words = Self.words(result.bestTranscription).map {
                TranscriptWord(text: $0.text, start: min($0.start, length), end: min($0.end, length),
                               confidence: $0.confidence)
            }
            segments = TranscriptAssembly.segments(from: words, offset: offset, keepWords: options.wordTimestamps)
        } else if let error, !Self.isNoSpeech(error), failure == nil {
            failure = error.localizedDescription
        }
        add(segments, through: Double(chunk.start + chunk.frames) / Double(sampleRate))
        active = nil
        startNext()
        checkFinished()
    }

    private func checkFinished() {
        guard inputEnded, active == nil, waiting.isEmpty, let finished else { return }
        self.finished = nil
        finished.resume()
    }

    static func words(_ transcription: SFTranscription) -> [TranscriptWord] {
        transcription.segments.map {
            // Partial results give no confidence (0); say so rather than claim it is zero.
            TranscriptWord(text: $0.substring, start: $0.timestamp, end: $0.timestamp + $0.duration,
                           confidence: $0.confidence > 0 ? Double($0.confidence) : .nan)
        }
    }

    /// Whether the error only says the request held no speech, which is not a failure for a stream with pauses.
    static func isNoSpeech(_ error: Error) -> Bool {
        let error = error as NSError
        return error.domain == "kAFAssistantErrorDomain" && (error.code == 1110 || error.code == 203)
    }
}
#endif
