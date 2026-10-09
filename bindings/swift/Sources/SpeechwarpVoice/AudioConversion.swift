#if canImport(AVFoundation)
import AVFoundation

/// Converts a stream of audio buffers from one format to another, keeping the converter's state between
/// buffers so that resampling has no seams.
final class AudioStreamConverter {
    let input: AVAudioFormat
    let output: AVAudioFormat
    private let converter: AVAudioConverter?

    /// Fails when the system cannot convert between the two formats.
    init?(from input: AVAudioFormat, to output: AVAudioFormat) {
        self.input = input
        self.output = output
        if input == output {
            converter = nil
        } else {
            guard let converter = AVAudioConverter(from: input, to: output) else { return nil }
            // Several input channels become one by mixing them, not by keeping the first.
            converter.downmix = true
            self.converter = converter
        }
    }

    /// Converts `buffer`. Returns nil when no output is ready yet (resampling holds back a few frames).
    func convert(_ buffer: AVAudioPCMBuffer) -> AVAudioPCMBuffer? {
        guard let converter else { return buffer.frameLength > 0 ? buffer : nil }
        return run(converter, buffer, endOfStream: false)
    }

    /// The frames still held back, at the end of the input.
    func flush() -> AVAudioPCMBuffer? {
        guard let converter else { return nil }
        return run(converter, nil, endOfStream: true)
    }

    private func run(_ converter: AVAudioConverter, _ buffer: AVAudioPCMBuffer?, endOfStream: Bool)
        -> AVAudioPCMBuffer? {
        let inputFrames = Double(buffer?.frameLength ?? 0)
        let capacity = AVAudioFrameCount(inputFrames * output.sampleRate / input.sampleRate) + 1024
        guard let out = AVAudioPCMBuffer(pcmFormat: output, frameCapacity: capacity) else { return nil }
        var given = false
        var error: NSError?
        converter.convert(to: out, error: &error) { _, status in
            if !given, let buffer {
                given = true
                status.pointee = .haveData
                return buffer
            }
            status.pointee = endOfStream ? .endOfStream : .noDataNow
            return nil
        }
        return error == nil && out.frameLength > 0 ? out : nil
    }
}

enum AudioSamples {
    /// One channel of 32-bit float at `sampleRate`, the format speechwarp's sessions take.
    static func monoFormat(sampleRate: Int) -> AVAudioFormat {
        AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: Double(sampleRate), channels: 1,
                      interleaved: false)!
    }

    /// A buffer holding `samples`, in `monoFormat(sampleRate:)`.
    static func buffer(_ samples: [Float], sampleRate: Int) -> AVAudioPCMBuffer? {
        let format = monoFormat(sampleRate: sampleRate)
        guard !samples.isEmpty,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(samples.count))
        else { return nil }
        buffer.frameLength = AVAudioFrameCount(samples.count)
        samples.withUnsafeBufferPointer { source in
            buffer.floatChannelData![0].update(from: source.baseAddress!, count: samples.count)
        }
        return buffer
    }

    /// The first channel as floats, whatever the buffer's sample format.
    static func mono(_ buffer: AVAudioPCMBuffer) -> [Float] {
        let count = Int(buffer.frameLength)
        if let data = buffer.floatChannelData {
            return Array(UnsafeBufferPointer(start: data[0], count: count))
        }
        if let data = buffer.int16ChannelData {
            return UnsafeBufferPointer(start: data[0], count: count).map { Float($0) / 32768 }
        }
        if let data = buffer.int32ChannelData {
            return UnsafeBufferPointer(start: data[0], count: count).map { Float($0) / 2147483648 }
        }
        return []
    }

    /// Root mean square of the samples: 0 for silence, about 0.7 for a full-scale sine.
    static func rms(_ samples: [Float]) -> Float {
        guard !samples.isEmpty else { return 0 }
        var sum: Float = 0
        for sample in samples { sum += sample * sample }
        return (sum / Float(samples.count)).squareRoot()
    }
}
#endif
