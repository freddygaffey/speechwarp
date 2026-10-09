#if canImport(AVFoundation)
import AVFoundation

/// Mono audio rendered from text, with the sample rate the voice produced it at.
public struct RenderedSpeech: Sendable {
    /// Samples from -1 to 1, one channel.
    public let samples: [Float]
    public let sampleRate: Int

    public var duration: TimeInterval { Double(samples.count) / Double(sampleRate) }
}

/// What can go wrong when speaking.
public enum SpeechVoiceError: Error, Equatable {
    /// The voice is not installed on this device.
    case voiceNotInstalled(String)
    /// The system produced no audio for the text.
    case noAudio
}

/// Turns text into audio with a system voice, into memory instead of to the speaker.
///
/// The system delivers the audio on the main thread's run loop, so that loop must be running (it always is in
/// an app). Renders are queued and done one at a time.
public final class SpeechRenderer: @unchecked Sendable {
    public let voice: SpeechVoice
    private let synthesizer = AVSpeechSynthesizer()
    private let systemVoice: AVSpeechSynthesisVoice

    public init(voice: SpeechVoice) throws {
        guard let systemVoice = AVSpeechSynthesisVoice(identifier: voice.identifier) else {
            throw SpeechVoiceError.voiceNotInstalled(voice.identifier)
        }
        self.voice = voice
        self.systemVoice = systemVoice
    }

    /// Renders `text` at the system rate `rate` (see `SpeechRate`). Empty or whitespace-only text gives no
    /// samples.
    public func render(_ text: String, rate: Float = SpeechRate.normal) async throws -> RenderedSpeech {
        if text.allSatisfy(\.isWhitespace) {
            return RenderedSpeech(samples: [], sampleRate: 22050)
        }
        let utterance = AVSpeechUtterance(string: text)
        utterance.voice = systemVoice
        utterance.rate = min(max(rate, SpeechRate.minimum), SpeechRate.maximum)
        return try await withCheckedThrowingContinuation { continuation in
            var samples: [Float] = []
            var sampleRate = 0
            var finished = false
            let deliver = {
                self.synthesizer.write(utterance) { buffer in
                    guard !finished, let pcm = buffer as? AVAudioPCMBuffer else { return }
                    if pcm.frameLength == 0 {
                        finished = true
                        if sampleRate == 0 {
                            continuation.resume(throwing: SpeechVoiceError.noAudio)
                        } else {
                            continuation.resume(returning: RenderedSpeech(samples: samples, sampleRate: sampleRate))
                        }
                        return
                    }
                    sampleRate = Int(pcm.format.sampleRate)
                    samples.append(contentsOf: Self.mono(pcm))
                }
            }
            if Thread.isMainThread { deliver() } else { DispatchQueue.main.async(execute: deliver) }
        }
    }

    /// The first channel as floats, whatever format the voice delivered.
    static func mono(_ pcm: AVAudioPCMBuffer) -> [Float] {
        let count = Int(pcm.frameLength)
        if let data = pcm.floatChannelData {
            return Array(UnsafeBufferPointer(start: data[0], count: count))
        }
        if let data = pcm.int16ChannelData {
            return UnsafeBufferPointer(start: data[0], count: count).map { Float($0) / 32768 }
        }
        if let data = pcm.int32ChannelData {
            return UnsafeBufferPointer(start: data[0], count: count).map { Float($0) / 2147483648 }
        }
        return []
    }
}
#endif
