#if canImport(AVFoundation)
import AVFoundation

/// A voice the system can speak with: one of Apple's Eloquence voices, or any other installed voice.
///
/// Eloquence is the compact synthesiser many fast screen-reader listeners choose, because it stays crisp at
/// high rates. Apple includes it from iOS 16 and macOS 13 (Eddy, Flo, Grandma, Grandpa, Reed, Rocko, Sandy
/// and Shelley, in several languages). The voices belong to the system: nothing is bundled.
public struct SpeechVoice: Hashable, Sendable {
    /// The system's identifier, such as "com.apple.eloquence.en-US.Reed".
    public let identifier: String
    /// The voice's name, such as "Reed".
    public let name: String
    /// A BCP 47 language tag, such as "en-US".
    public let language: String

    /// Whether this is one of the Eloquence voices.
    public var isEloquence: Bool { identifier.contains(".eloquence.") }

    /// Every voice installed on this device.
    public static var all: [SpeechVoice] {
        AVSpeechSynthesisVoice.speechVoices().map(SpeechVoice.init)
    }

    /// The Eloquence voices installed on this device, optionally only those for a language ("en", "en-GB").
    public static func eloquence(language: String? = nil) -> [SpeechVoice] {
        all.filter { $0.isEloquence && (language.map($0.language.hasPrefix) ?? true) }
    }

    /// The voice with this identifier, if it is installed.
    public init?(identifier: String) {
        guard let voice = AVSpeechSynthesisVoice(identifier: identifier) else { return nil }
        self.init(voice)
    }

    init(_ voice: AVSpeechSynthesisVoice) {
        identifier = voice.identifier
        name = voice.name
        language = voice.language
    }
}

/// The rates the system accepts for a voice: 0.5 is ordinary speech and the maximum, 1, is several times
/// faster (about four times for Eloquence). Above that, `SpokenText.speed` speeds the audio up further.
public enum SpeechRate {
    public static let minimum: Float = AVSpeechUtteranceMinimumSpeechRate
    public static let normal: Float = AVSpeechUtteranceDefaultSpeechRate
    public static let maximum: Float = AVSpeechUtteranceMaximumSpeechRate
}
#endif
