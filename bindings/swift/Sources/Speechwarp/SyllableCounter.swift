import CSpeechwarp

/// The syllable-rate estimator behind `SpeechwarpStream.syllableRate`, on its own, for audio that does not go
/// through a stream (a player using some other speed-up, or measuring a file). A counter given the same input
/// as a stream reports the same rate as the stream does with a window of 60 s and a minimum of 10 s. Float
/// input is converted to 16 bits exactly as the stream converts it.
///
/// Samples are interleaved: a frame is one sample per channel.
///
/// A counter is not thread safe. Use it from one thread, or lock around it.
public final class SyllableCounter {
    public let sampleRate: Int
    /// Samples in a frame.
    public let channels: Int

    private let counter: OpaquePointer

    /// Creates a counter. Throws `SpeechwarpError.unsupportedFormat` if the sample rate is not 4000 to 384000 or
    /// the channel count is not 1 to 32.
    public init(sampleRate: Int, channels: Int = 1) throws {
        guard (4000...384000).contains(sampleRate), (1...32).contains(channels) else {
            throw SpeechwarpError.unsupportedFormat
        }
        guard let counter = speechwarp_syllables_create(Int32(sampleRate), Int32(channels)) else {
            throw SpeechwarpError.outOfMemory
        }
        self.counter = counter
        self.sampleRate = sampleRate
        self.channels = channels
    }

    deinit {
        speechwarp_syllables_destroy(counter)
    }

    /// Adds input.
    public func write(_ samples: UnsafeBufferPointer<Float>) throws {
        let frames = try wholeFrames(samples.count)
        guard speechwarp_syllables_write(counter, samples.baseAddress, frames) != 0 else {
            throw SpeechwarpError.outOfMemory
        }
    }

    public func write(_ samples: UnsafeBufferPointer<Int16>) throws {
        let frames = try wholeFrames(samples.count)
        guard speechwarp_syllables_write_i16(counter, samples.baseAddress, frames) != 0 else {
            throw SpeechwarpError.outOfMemory
        }
    }

    public func write(_ samples: [Float]) throws {
        try samples.withUnsafeBufferPointer { try write($0) }
    }

    public func write(_ samples: [Int16]) throws {
        try samples.withUnsafeBufferPointer { try write($0) }
    }

    /// Syllables a second over the last `windowSeconds` written (or all of it, if less), or nil until
    /// `minimumSeconds` have been written. The window is clamped to 1 to 120 s, the minimum to 0 up to the
    /// window. Multiply by the speed for the rate heard.
    public func rate(windowSeconds: Double = 60, minimumSeconds: Double = 10) -> Double? {
        let rate = speechwarp_syllables_rate(counter, windowSeconds, minimumSeconds)
        return rate < 0 ? nil : rate
    }

    /// Forgets everything written.
    public func reset() {
        speechwarp_syllables_reset(counter)
    }

    private func wholeFrames(_ samples: Int) throws -> Int32 {
        guard samples % channels == 0, let frames = Int32(exactly: samples / channels) else {
            throw SpeechwarpError.partialFrame
        }
        return frames
    }
}
