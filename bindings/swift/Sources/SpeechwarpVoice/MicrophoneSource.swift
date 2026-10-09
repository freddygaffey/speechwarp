#if canImport(AVFoundation) && (os(iOS) || os(macOS))
import AVFoundation
import Speechwarp

/// What can go wrong when listening to the microphone.
public enum MicrophoneSourceError: Error, Equatable {
    /// The user has refused the microphone, or it is restricted.
    case notAuthorised
    /// The app's Info.plist lacks `NSMicrophoneUsageDescription`, which the system needs before it will ask.
    case missingUsageDescription(String)
    /// There is no audio input, or the system would not start it, with the system's description.
    case noInput(String)
    /// `start` was called while already running.
    case alreadyRunning
}

/// The default audio input, captured with `AVAudioEngine`, mixed to one channel and converted to float at
/// `sampleRate`, then given to a transcription session (of any engine) or a handler of your own.
///
/// An app needs `NSMicrophoneUsageDescription` in its Info.plist. On iOS the shared audio session is set to
/// play and record while running, unless `configuresAudioSession` is false.
///
/// ```swift
/// let session = try transcriber.startSession(sampleRate: 16000, options: .init())
/// let microphone = MicrophoneSource(sampleRate: 16000)
/// try await microphone.start(feeding: session)
/// // ... show session.partial, take session.takeSegments() ...
/// microphone.stop()
/// let rest = try await session.finish()
/// ```
public final class MicrophoneSource: @unchecked Sendable {
    /// The rate samples are delivered at. Start the session at the same rate.
    public let sampleRate: Int
    /// On iOS, whether `start` sets the shared audio session to play and record (and `stop` deactivates it).
    public var configuresAudioSession = true

    private let lock = NSLock()
    private var engine: AVAudioEngine?
    private var currentLevel: Float = 0

    /// A source delivering mono float at `sampleRate` (16000 suits every engine here).
    public init(sampleRate: Int = 16000) {
        precondition(sampleRate > 0, "sampleRate must be positive")
        self.sampleRate = sampleRate
    }

    /// Whether it is capturing.
    public var isRunning: Bool { lock.synchronized { engine != nil } }

    /// The loudness of the latest audio, 0 to 1 (root mean square; ordinary speech is about 0.02 to 0.2),
    /// for a level meter. 0 when stopped.
    public var level: Float { lock.synchronized { currentLevel } }

    /// Asks for the microphone if the user has not yet been asked. Returns whether it may be used.
    /// Throws `missingUsageDescription` in an app whose Info.plist lacks the key (asking would end the app).
    public static func requestPermission() async throws -> Bool {
        let key = "NSMicrophoneUsageDescription"
        if AppleTranscriber.isApp && Bundle.main.object(forInfoDictionaryKey: key) == nil {
            throw MicrophoneSourceError.missingUsageDescription(key)
        }
        #if os(iOS)
        if #available(iOS 17, *) {
            return await AVAudioApplication.requestRecordPermission()
        }
        return await withCheckedContinuation { continuation in
            AVAudioSession.sharedInstance().requestRecordPermission { continuation.resume(returning: $0) }
        }
        #else
        return await AVCaptureDevice.requestAccess(for: .audio)
        #endif
    }

    /// Asks for permission if needed and starts capturing into `session`, which must have been started at
    /// `sampleRate`. Stopping does not finish the session; call its `finish` afterwards.
    public func start(feeding session: TranscriptionSession) async throws {
        try await start { samples in session.write(samples) }
    }

    /// Asks for permission if needed and starts capturing, calling `handler` on an audio thread with each
    /// stretch of samples (mono, -1 to 1, at `sampleRate`, typically 0.1 s). Keep the handler quick.
    public func start(_ handler: @escaping @Sendable ([Float]) -> Void) async throws {
        guard try await Self.requestPermission() else { throw MicrophoneSourceError.notAuthorised }
        try lock.synchronized {
            guard engine == nil else { throw MicrophoneSourceError.alreadyRunning }
            #if os(iOS)
            if configuresAudioSession {
                let audioSession = AVAudioSession.sharedInstance()
                do {
                    try audioSession.setCategory(.playAndRecord, mode: .default, options: [.defaultToSpeaker])
                    try audioSession.setActive(true)
                } catch {
                    throw MicrophoneSourceError.noInput(error.localizedDescription)
                }
            }
            #endif
            let engine = AVAudioEngine()
            let input = engine.inputNode
            let format = input.outputFormat(forBus: 0)
            guard format.sampleRate > 0, format.channelCount > 0 else {
                throw MicrophoneSourceError.noInput("No audio input is available")
            }
            guard let converter = AudioStreamConverter(from: format, to: AudioSamples.monoFormat(sampleRate: sampleRate))
            else { throw MicrophoneSourceError.noInput("Cannot convert the input's format \(format)") }
            let frames = AVAudioFrameCount(format.sampleRate / 10)
            input.installTap(onBus: 0, bufferSize: frames, format: format) { [weak self] buffer, _ in
                guard let converted = converter.convert(buffer) else { return }
                let samples = AudioSamples.mono(converted)
                if let self { self.lock.synchronized { self.currentLevel = AudioSamples.rms(samples) } }
                handler(samples)
            }
            engine.prepare()
            do {
                try engine.start()
            } catch {
                input.removeTap(onBus: 0)
                throw MicrophoneSourceError.noInput(error.localizedDescription)
            }
            self.engine = engine
        }
    }

    /// Stops capturing. Safe to call when not running.
    public func stop() {
        let engine: AVAudioEngine? = lock.synchronized {
            defer { self.engine = nil; currentLevel = 0 }
            return self.engine
        }
        guard let engine else { return }
        engine.inputNode.removeTap(onBus: 0)
        engine.stop()
        #if os(iOS)
        if configuresAudioSession {
            try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        }
        #endif
    }

    deinit {
        stop()
    }
}
#endif
