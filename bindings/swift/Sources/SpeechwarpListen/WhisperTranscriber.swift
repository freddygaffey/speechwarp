#if os(iOS) || os(macOS)
import Foundation
import Speechwarp
import speechwarp_listen

/// A loaded model. Freed when the transcriber and every session and transcription using it have let go.
final class ModelBox: @unchecked Sendable {
    let pointer: OpaquePointer

    init(_ pointer: OpaquePointer) {
        self.pointer = pointer
    }

    deinit {
        speechwarp_listen_model_free(pointer)
    }
}

/// Native options, freed when the last user lets go.
final class OptionsBox: @unchecked Sendable {
    let pointer: OpaquePointer

    init(_ options: TranscriptionOptions) throws {
        guard let pointer = speechwarp_listen_options_create() else { throw WhisperError.outOfMemory }
        self.pointer = pointer
        if speechwarp_listen_options_set_language(pointer, options.language) != SPEECHWARP_LISTEN_OK {
            throw WhisperError.unknownLanguage(options.language ?? "")
        }
        speechwarp_listen_options_set_word_timestamps(pointer, options.wordTimestamps ? 1 : 0)
        // Whisper takes the prompt as the text spoken just before, so a list of words separated by commas works.
        if !options.hints.isEmpty {
            speechwarp_listen_options_set_prompt(pointer, options.hints.joined(separator: ", "))
        }
        speechwarp_listen_options_set_preset(pointer, options.preset.native)
        speechwarp_listen_options_set_threads(pointer, Int32(clamping: max(0, options.threads)))
    }

    deinit {
        speechwarp_listen_options_free(pointer)
    }
}

extension TranscriptionPreset {
    var native: Int32 {
        switch self {
        case .fast: return SPEECHWARP_LISTEN_PRESET_FAST
        case .balanced: return SPEECHWARP_LISTEN_PRESET_BALANCED
        case .accurate: return SPEECHWARP_LISTEN_PRESET_ACCURATE
        }
    }
}

/// Turns native results into the shared types.
enum Results {
    /// The segments of a result, which is then freed.
    static func take(_ result: OpaquePointer) -> [TranscriptSegment] {
        defer { speechwarp_listen_result_free(result) }
        return (0..<speechwarp_listen_result_segment_count(result)).map { s in
            let words = (0..<speechwarp_listen_result_word_count(result, s)).map { w in
                TranscriptWord(text: String(cString: speechwarp_listen_result_word_text(result, s, w)),
                               start: speechwarp_listen_result_word_start(result, s, w),
                               end: speechwarp_listen_result_word_end(result, s, w),
                               confidence: speechwarp_listen_result_word_probability(result, s, w))
            }
            return TranscriptSegment(text: String(cString: speechwarp_listen_result_segment_text(result, s)),
                                     start: speechwarp_listen_result_segment_start(result, s),
                                     end: speechwarp_listen_result_segment_end(result, s),
                                     words: words)
        }
    }

    /// Several segments as one, for a partial guess; nil if there are none.
    static func merge(_ segments: [TranscriptSegment]) -> TranscriptSegment? {
        guard let first = segments.first, let last = segments.last else { return nil }
        if segments.count == 1 { return first }
        return TranscriptSegment(text: segments.map(\.text).joined(separator: " "), start: first.start, end: last.end,
                                 words: segments.flatMap(\.words))
    }
}

/// Speech to text with whisper.cpp, through the engine-independent `Transcriber` interface. Everything runs on the
/// device; the app supplies the model file (see `WhisperModels`).
///
/// A passage given at once (a sentence said back for the trainer, a clip) goes to `transcribe`. Anything long or
/// live (a whole book decoded in chunks, a microphone) goes to a session from `startSession`: it recognises the
/// audio in chunks of 20 to 30 s, each cut at the quietest moment so that no word is split, on a thread of its own,
/// so `write` never waits.
///
/// Thread safe: any number of transcriptions and sessions may run at once on one transcriber, each with its own
/// working memory (tens of MB for small models, hundreds for large ones). The model is freed once the transcriber
/// and every session on it are gone. Needs iOS 16.4 or macOS 13.3, as whisper.cpp does.
///
/// ```swift
/// let transcriber = try WhisperTranscriber(modelPath: path)
/// try await transcriber.prepare(progress: nil)
/// let transcript = try await transcriber.transcribe(samples, sampleRate: 44100, options: .init())
/// ```
@available(macOS 13.3, iOS 16.4, *)
public final class WhisperTranscriber: Transcriber, @unchecked Sendable {
    public let model: TranscriptionModel
    /// The model file.
    public let modelPath: String
    /// CPU threads for transcriptions that do not set their own; 0 lets the library choose.
    public let threads: Int
    /// Whether the GPU (Metal) is to be used; it is not in the simulator.
    public let useGPU: Bool

    private let lock = NSLock()
    private var loaded: ModelBox?
    private var loading: Task<ModelBox, Error>?

    /// A transcriber for the whisper.cpp model file at `modelPath`. Nothing is loaded until `prepare`.
    ///
    /// - Parameters:
    ///   - model: what the file is; by default the catalogue model with the same file name, or a description of the
    ///     file (see `WhisperModels.forFile`).
    ///   - threads: CPU threads for transcriptions that do not set their own; 0 for the number of cores, at most 8.
    ///   - useGPU: use the GPU through Metal (not in the simulator, where it is ignored). Much faster on a Mac with
    ///     Apple silicon.
    public init(modelPath: String, model: TranscriptionModel? = nil, threads: Int = 0, useGPU: Bool = false) throws {
        if let model, model.engine != .whisper { throw WhisperError.notAWhisperModel(model.id) }
        self.modelPath = modelPath
        self.model = model ?? WhisperModels.forFile(modelPath)
        self.threads = max(0, threads)
        self.useGPU = useGPU
    }

    /// The version of the native library, the same as the package's, such as "0.3.7".
    public static var nativeVersion: String { String(cString: speechwarp_listen_version()) }

    /// The version of whisper.cpp in the native library, such as "1.9.5".
    public static var engineVersion: String { String(cString: speechwarp_listen_engine_version()) }

    /// What the native library can use on this machine (CPU features, GPU), as one line, for diagnostics.
    public static var systemInfo: String { String(cString: speechwarp_listen_system_info()) }

    /// Sends whisper.cpp's own log to standard error when true; it goes nowhere when false (the default). Applies to
    /// the whole process.
    public static func setEngineLog(_ on: Bool) {
        speechwarp_listen_set_log(on ? 1 : 0)
    }

    public var isReady: Bool { lock.withLock { loaded != nil } }

    /// Whether the loaded model understands many languages (false for the ".en" models), or nil before `prepare`
    /// has completed.
    public var isMultilingual: Bool? {
        lock.withLock { loaded }.map { speechwarp_listen_model_multilingual($0.pointer) != 0 }
    }

    /// Loads the model, off the caller's thread: from a fraction of a second (tiny) to several seconds (large).
    /// Calling it again once loaded does nothing. Progress goes from 0 to 1 with nothing between, as whisper.cpp
    /// does not report it. The load cannot be cancelled once started.
    public func prepare(progress: (@Sendable (Double) -> Void)?) async throws {
        progress?(0)
        _ = try await loadedModel()
        progress?(1)
    }

    private func loadedModel() async throws -> ModelBox {
        let task: Task<ModelBox, Error> = lock.withLock {
            if let loaded { return Task { loaded } }
            if let loading { return loading }
            let path = modelPath, threads = Int32(clamping: threads)
#if targetEnvironment(simulator)
            // whisper.cpp's GPU code fails in the simulator (its own examples turn it off there).
            let flags: Int32 = 0
#else
            let flags = useGPU ? SPEECHWARP_LISTEN_LOAD_GPU : 0
#endif
            let task = Task { try await Self.load(path: path, threads: threads, flags: Int32(flags)) }
            loading = task
            return task
        }
        do {
            let model = try await task.value
            lock.withLock {
                loaded = loaded ?? model
                loading = nil
            }
            return model
        } catch {
            lock.withLock { loading = nil }
            throw error
        }
    }

    private static func load(path: String, threads: Int32, flags: Int32) async throws -> ModelBox {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                guard FileManager.default.fileExists(atPath: path) else {
                    continuation.resume(throwing: WhisperError.modelFileMissing(path))
                    return
                }
                if let pointer = speechwarp_listen_model_load(path, threads, flags) {
                    continuation.resume(returning: ModelBox(pointer))
                } else {
                    continuation.resume(throwing: WhisperError.invalidModelFile(path))
                }
            }
        }
    }

    /// Transcribes a whole passage given at once: a sentence said back, a clip. Prepares first if needed. Mono
    /// samples, -1 to 1, at any rate from 4000 to 384000 Hz. The audio and a 16 kHz copy are held in memory, so
    /// use a session for more than a few minutes. Cancelling the task stops it soon, throwing `CancellationError`.
    public func transcribe(_ samples: [Float], sampleRate: Int, options: TranscriptionOptions) async throws
        -> Transcript {
        try Self.check(sampleRate)
        let model = try await loadedModel()
        let native = try OptionsBox(options)
        try Task.checkCancellation()
        if samples.isEmpty { return Transcript(segments: []) }
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Transcript, Error>) in
                // Off the cooperative pool: a book can take hours.
                DispatchQueue.global(qos: .userInitiated).async {
                    var error: Int32 = SPEECHWARP_LISTEN_OK
                    let result = samples.withUnsafeBufferPointer {
                        speechwarp_listen_transcribe(model.pointer, $0.baseAddress, Int64($0.count), Int32(sampleRate),
                                                     native.pointer, &error)
                    }
                    if let result {
                        continuation.resume(returning: Transcript(segments: Results.take(result)))
                    } else if error == SPEECHWARP_LISTEN_ERROR_CANCELLED {
                        continuation.resume(throwing: CancellationError())
                    } else {
                        continuation.resume(throwing: WhisperError.code(error))
                    }
                }
            }
        } onCancel: {
            speechwarp_listen_options_cancel(native.pointer)
        }
    }

    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone. Any length.
    /// Writing never waits; a thread of the session's own recognises each chunk of 20 to 30 s as it completes and
    /// calls `onSegmentsReady`. Audio not yet recognised is held at 16 kHz (about 230 MB an hour), so a caller
    /// decoding a book faster than it is recognised should keep `secondsWritten - secondsRecognised` to a few
    /// minutes. `partial` is worked out only while someone reads it, so a book costs nothing extra. The session
    /// stops when `finish` completes, or when `close` is called or the session is released.
    public func startSession(sampleRate: Int, options: TranscriptionOptions) throws -> TranscriptionSession {
        try Self.check(sampleRate)
        guard let model = lock.withLock({ loaded }) else { throw WhisperError.notPrepared }
        let native = try OptionsBox(options)
        guard let session = speechwarp_listen_session_create(model.pointer, Int32(sampleRate), native.pointer) else {
            throw WhisperError.outOfMemory
        }
        return WhisperSession(SessionCore(session: session, model: model))
    }

    private static func check(_ sampleRate: Int) throws {
        guard (4000...384_000).contains(sampleRate) else { throw WhisperError.unsupportedSampleRate(sampleRate) }
    }
}
#endif
