#if canImport(Speech) && (os(iOS) || os(macOS))
import AVFoundation
import Foundation
import Speech
import Speechwarp

/// What can go wrong with Apple's speech recogniser.
public enum AppleTranscriberError: Error, Equatable {
    /// The model given is not one of Apple's (its engine is another).
    case notAnAppleModel(String)
    /// The user has refused speech recognition, or it is restricted on this device.
    case notAuthorised
    /// The app's Info.plist lacks this key, which the system needs before it will ask the user.
    case missingUsageDescription(String)
    /// The system cannot recognise this language on the device.
    case unsupportedLanguage(String)
    /// `prepare` has not completed.
    case notPrepared
    /// The recogniser failed, with the system's description.
    case recognitionFailed(String)
}

/// Apple's on-device speech recogniser, through the engine-independent `Transcriber` interface.
///
/// On iOS 26 and macOS 26 and later it uses `SpeechAnalyzer` with `SpeechTranscriber` (or, on devices without
/// that, `DictationTranscriber`); the system downloads the language's model the first time, during `prepare`.
/// On earlier systems it uses `SFSpeechRecognizer` with on-device recognition required, which is made for
/// utterances, so long input is given to it in requests of 20 to 50 s cut in pauses. Either way nothing leaves
/// the device.
///
/// An iOS app needs `NSSpeechRecognitionUsageDescription` in its Info.plist (and, for the microphone,
/// `NSMicrophoneUsageDescription`; see `MicrophoneSource`).
///
/// ```swift
/// let transcriber = try AppleTranscriber(model: AppleTranscriber.model(language: "en-GB"))
/// try await transcriber.prepare(progress: nil)
/// let transcript = try await transcriber.transcribe(samples, sampleRate: 16000, options: .init())
/// ```
public final class AppleTranscriber: Transcriber, @unchecked Sendable {
    /// Which of Apple's recognisers to use.
    public enum Recogniser: Sendable, Equatable {
        /// `SpeechAnalyzer` where the system has it and supports the language, otherwise `SFSpeechRecognizer`.
        case automatic
        /// `SpeechAnalyzer` (iOS 26, macOS 26) only.
        case speechAnalyzer
        /// `SFSpeechRecognizer`, on-device, only.
        case speechRecognizer
    }

    /// The analyser's module: the newer, long-form `SpeechTranscriber`, or `DictationTranscriber` where the
    /// device lacks that.
    enum AnalyzerModule {
        case transcriber, dictation
    }

    public let model: TranscriptionModel
    /// The recogniser asked for.
    public let recogniser: Recogniser
    /// The language the model is for, as a locale.
    public let locale: Locale

    private let lock = NSLock()
    private var ready = false
    private var chosen: Recogniser?
    private var analyzerModule: AnalyzerModule = .transcriber
    private var analyzerLocale: Locale

    public var isReady: Bool { lock.synchronized { ready } }

    /// The recogniser `prepare` chose: `.speechAnalyzer` or `.speechRecognizer`, or nil before it has run.
    public var recogniserInUse: Recogniser? { lock.synchronized { chosen } }

    /// A transcriber for `model`, which must be one of Apple's (see `models` and `model(language:)`).
    public init(model: TranscriptionModel, recogniser: Recogniser = .automatic) throws {
        guard model.engine == .apple else { throw AppleTranscriberError.notAnAppleModel(model.id) }
        self.model = model
        self.recogniser = recogniser
        let tag = model.languages.first ?? String(model.id.dropFirst("apple-".count))
        locale = Locale(identifier: tag)
        analyzerLocale = locale
    }

    // MARK: Models

    /// One model per language this device can recognise on the device with `SFSpeechRecognizer`, which every
    /// supported system has. `availableModels()` adds the languages of the newer recogniser.
    public static var models: [TranscriptionModel] {
        recognizerLocales().map(model(for:)).sorted { $0.id < $1.id }
    }

    /// Every language this device can recognise on the device, with either recogniser.
    public static func availableModels() async -> [TranscriptionModel] {
        var tags = Set(recognizerLocales().map(bcp47))
        if #available(macOS 26, iOS 26, *) {
            for locale in await SpeechTranscriber.supportedLocales { tags.insert(bcp47(locale)) }
            for locale in await DictationTranscriber.supportedLocales { tags.insert(bcp47(locale)) }
        }
        return tags.sorted().map { model(for: Locale(identifier: $0)) }
    }

    /// The model for a BCP 47 language tag ("en-GB"), whether or not this device supports it; `prepare` says.
    public static func model(language: String) -> TranscriptionModel {
        model(for: Locale(identifier: language))
    }

    private static func model(for locale: Locale) -> TranscriptionModel {
        let tag = bcp47(locale)
        let language = Locale(identifier: "en").localizedString(forIdentifier: tag) ?? tag
        return TranscriptionModel(id: "apple-\(tag)", engine: .apple, name: "Apple on-device, \(language)",
                                  languages: [tag], sizeBytes: 0, downloadURL: nil, sha256: nil,
                                  relativeSpeed: 1)
    }

    private static func recognizerLocales() -> [Locale] {
        SFSpeechRecognizer.supportedLocales().filter { SFSpeechRecognizer(locale: $0)?.supportsOnDeviceRecognition ?? false }
    }

    static func bcp47(_ locale: Locale) -> String {
        locale.identifier.replacingOccurrences(of: "_", with: "-")
    }

    // MARK: Preparing

    /// Asks for permission to recognise speech if it has not been given, chooses the recogniser and, for
    /// `SpeechAnalyzer`, downloads and installs the language's model if the system lacks it, reporting 0 to 1.
    ///
    /// The system asks the user only once; a refusal throws `notAuthorised`, and the user must then allow it
    /// in Settings. A process that is not an app (a command-line tool, a test) goes ahead without asking, as
    /// macOS permits on-device recognition there.
    public func prepare(progress: (@Sendable (Double) -> Void)?) async throws {
        try await Self.authorise()

        if recogniser != .speechRecognizer, #available(macOS 26, iOS 26, *) {
            if let (module, supported) = await analyzerChoice() {
                try await installAssets(module: module, locale: supported, progress: progress)
                lock.synchronized {
                    analyzerModule = module
                    analyzerLocale = supported
                    chosen = .speechAnalyzer
                    ready = true
                }
                progress?(1)
                return
            }
        }
        if recogniser == .speechAnalyzer {
            throw AppleTranscriberError.unsupportedLanguage(model.languages.first ?? model.id)
        }
        guard let recognizer = SFSpeechRecognizer(locale: locale), recognizer.supportsOnDeviceRecognition else {
            throw AppleTranscriberError.unsupportedLanguage(model.languages.first ?? model.id)
        }
        lock.synchronized {
            chosen = .speechRecognizer
            ready = true
        }
        progress?(1)
    }

    static func authorise() async throws {
        switch SFSpeechRecognizer.authorizationStatus() {
        case .authorized:
            return
        case .denied, .restricted:
            throw AppleTranscriberError.notAuthorised
        default:
            break
        }
        // Asking without the usage description ends the app, so check for it first.
        let key = "NSSpeechRecognitionUsageDescription"
        if Bundle.main.object(forInfoDictionaryKey: key) == nil {
            if isApp { throw AppleTranscriberError.missingUsageDescription(key) }
            return
        }
        let status = await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { continuation.resume(returning: $0) }
        }
        if status != .authorized { throw AppleTranscriberError.notAuthorised }
    }

    /// Whether this process is an app, as opposed to a command-line tool or a test runner.
    static var isApp: Bool {
        #if os(iOS)
        return true
        #else
        return Bundle.main.bundleURL.pathExtension == "app"
        #endif
    }

    @available(macOS 26, iOS 26, *)
    private func analyzerChoice() async -> (AnalyzerModule, Locale)? {
        if SpeechTranscriber.isAvailable, let supported = await SpeechTranscriber.supportedLocale(equivalentTo: locale) {
            return (.transcriber, supported)
        }
        if let supported = await DictationTranscriber.supportedLocale(equivalentTo: locale) {
            return (.dictation, supported)
        }
        return nil
    }

    @available(macOS 26, iOS 26, *)
    private func installAssets(module: AnalyzerModule, locale: Locale,
                               progress: (@Sendable (Double) -> Void)?) async throws {
        let modules = AnalyzerSession.modules(module, locale: locale, options: TranscriptionOptions())
        if await AssetInventory.status(forModules: modules) < .installed,
           !(await AssetInventory.reservedLocales).contains(locale) {
            // An app may keep only a few languages' models; asking for one reserves it. If the system refuses,
            // the installation request below says so.
            _ = try? await AssetInventory.reserve(locale: locale)
        }
        guard let request = try await AssetInventory.assetInstallationRequest(supporting: modules) else { return }
        let watch = progress.map { report in
            Task {
                while !Task.isCancelled {
                    report(request.progress.fractionCompleted)
                    try? await Task.sleep(nanoseconds: 200_000_000)
                }
            }
        }
        defer { watch?.cancel() }
        do {
            try await request.downloadAndInstall()
        } catch {
            throw AppleTranscriberError.recognitionFailed(error.localizedDescription)
        }
    }

    // MARK: Transcribing

    /// Transcribes a whole passage given at once: a sentence said back, a clip. Prepares first if needed.
    /// Mono samples, -1 to 1, at any rate. The model fixes the language, so `options.language` is not used.
    public func transcribe(_ samples: [Float], sampleRate: Int, options: TranscriptionOptions) async throws
        -> Transcript {
        if !isReady { try await prepare(progress: nil) }
        let session = try startSession(sampleRate: sampleRate, options: options)
        session.write(samples)
        return try await session.finish()
    }

    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone (see
    /// `MicrophoneSource`). Throws `notPrepared` before `prepare` has completed.
    ///
    /// Writing never waits, so a caller decoding a book faster than it is recognised should keep
    /// `secondsWritten - secondsRecognised` to a few minutes, or memory grows with the backlog.
    public func startSession(sampleRate: Int, options: TranscriptionOptions) throws -> TranscriptionSession {
        precondition(sampleRate > 0, "sampleRate must be positive")
        // Read one at a time with explicit types: a single tuple here crashed the Swift 6.1 type checker.
        lock.lock()
        let isReady: Bool = ready
        let chosen: Recogniser? = self.chosen
        let module: AnalyzerModule = analyzerModule
        let analyzerLocale: Locale = self.analyzerLocale
        lock.unlock()
        guard isReady else { throw AppleTranscriberError.notPrepared }
        if chosen == .speechAnalyzer, #available(macOS 26, iOS 26, *) {
            return AnalyzerSession(module: module, locale: analyzerLocale, sampleRate: sampleRate, options: options)
        }
        guard let recognizer = SFSpeechRecognizer(locale: locale) else {
            throw AppleTranscriberError.unsupportedLanguage(model.languages.first ?? model.id)
        }
        return RecognizerSession(recognizer: recognizer, sampleRate: sampleRate, options: options)
    }
}

extension NSLock {
    func synchronized<T>(_ body: () throws -> T) rethrows -> T {
        lock()
        defer { unlock() }
        return try body()
    }
}
#endif
