// The engine-independent face of speech-to-text. Engines conform to Transcriber: Apple's on-device recogniser
// (SpeechwarpVoice) and whisper.cpp (SpeechwarpListen). An app codes against these types and picks an engine,
// or lets the user pick one, at run time. The C# package has the same types (Speechwarp.Transcription).

/// Which engine a model belongs to.
public enum TranscriptionEngine: String, Sendable {
    /// Apple's on-device speech recogniser. The system manages its models.
    case apple
    /// whisper.cpp. The app downloads the model file and passes its path.
    case whisper
}

/// How to trade speed against accuracy, where an engine offers the choice.
public enum TranscriptionPreset: Sendable {
    case fast, balanced, accurate
}

/// A model an engine can use.
public struct TranscriptionModel: Hashable, Sendable {
    /// Stable identifier, such as "whisper-base.en" or "apple-en-US".
    public let id: String
    public let engine: TranscriptionEngine
    /// A name to show, such as "Whisper base (English)".
    public let name: String
    /// BCP 47 tags it understands; empty means many (it detects the language).
    public let languages: [String]
    /// Download size, or 0 when the system manages it.
    public let sizeBytes: Int64
    /// Where the app can download it, or nil when the system manages it.
    public let downloadURL: String?
    /// Lower-case hex SHA-256 of the download, or nil.
    public let sha256: String?
    /// Rough speed against the engine's fastest model, 1 = fastest.
    public let relativeSpeed: Double

    public init(id: String, engine: TranscriptionEngine, name: String, languages: [String], sizeBytes: Int64,
                downloadURL: String?, sha256: String?, relativeSpeed: Double) {
        self.id = id
        self.engine = engine
        self.name = name
        self.languages = languages
        self.sizeBytes = sizeBytes
        self.downloadURL = downloadURL
        self.sha256 = sha256
        self.relativeSpeed = relativeSpeed
    }
}

/// Settings for a transcription. Every field has a sensible default.
public struct TranscriptionOptions: Sendable {
    /// BCP 47 tag of the speech ("en", "en-US"), or nil to detect it where the engine can.
    public var language: String?
    /// Times for each word as well as each segment.
    public var wordTimestamps = true
    /// Words likely to appear (names, invented words) to help the recogniser. May be ignored.
    public var hints: [String] = []
    public var preset: TranscriptionPreset = .balanced
    /// CPU threads, where the engine uses them; 0 lets it choose.
    public var threads = 0

    public init(language: String? = nil, wordTimestamps: Bool = true, hints: [String] = [],
                preset: TranscriptionPreset = .balanced, threads: Int = 0) {
        self.language = language
        self.wordTimestamps = wordTimestamps
        self.hints = hints
        self.preset = preset
        self.threads = threads
    }
}

/// A word with its time in the audio, in seconds from the start of what was given.
public struct TranscriptWord: Hashable, Sendable {
    public let text: String
    public let start: Double
    public let end: Double
    /// 0 to 1, or NaN when the engine does not say.
    public let confidence: Double

    public init(text: String, start: Double, end: Double, confidence: Double) {
        self.text = text
        self.start = start
        self.end = end
        self.confidence = confidence
    }
}

/// A stretch of recognised speech, usually a phrase or sentence.
public struct TranscriptSegment: Hashable, Sendable {
    public let text: String
    public let start: Double
    public let end: Double
    /// Empty unless word timestamps were asked for and the engine gives them.
    public let words: [TranscriptWord]

    public init(text: String, start: Double, end: Double, words: [TranscriptWord]) {
        self.text = text
        self.start = start
        self.end = end
        self.words = words
    }
}

/// What was recognised.
public struct Transcript: Hashable, Sendable {
    public let segments: [TranscriptSegment]
    /// All the segments' text, joined with spaces.
    public var text: String {
        segments.map { $0.text.trimmingCharacters(in: .whitespacesAndNewlines) }.joined(separator: " ")
    }

    public init(segments: [TranscriptSegment]) {
        self.segments = segments
    }
}

/// A speech recogniser.
public protocol Transcriber: AnyObject, Sendable {
    var model: TranscriptionModel { get }
    /// Whether `prepare` has completed.
    var isReady: Bool { get }
    /// Gets ready: loads the model, or for Apple asks permission and installs the system's assets if needed.
    func prepare(progress: (@Sendable (Double) -> Void)?) async throws
    /// Transcribes a whole passage given at once. Mono samples, -1 to 1.
    func transcribe(_ samples: [Float], sampleRate: Int, options: TranscriptionOptions) async throws -> Transcript
    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone.
    func startSession(sampleRate: Int, options: TranscriptionOptions) throws -> TranscriptionSession
}

/// Audio in over time, recognised text out as it is ready.
public protocol TranscriptionSession: AnyObject, Sendable {
    /// Adds mono samples, -1 to 1, of any length. Never waits for recognition.
    func write(_ samples: [Float])
    /// Segments finished since the last call, in order. Times are from the start of the session.
    func takeSegments() -> [TranscriptSegment]
    /// The engine's current guess at speech not yet finished, for live display.
    var partial: TranscriptSegment? { get }
    var secondsWritten: Double { get }
    var secondsRecognised: Double { get }
    /// Called on a background thread whenever `takeSegments` has something new.
    var onSegmentsReady: (@Sendable () -> Void)? { get set }
    /// Ends the input and returns everything not yet taken.
    func finish() async throws -> Transcript
}

import Foundation
