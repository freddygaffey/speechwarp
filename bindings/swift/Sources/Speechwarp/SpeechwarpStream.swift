import CSpeechwarp

/// What can go wrong.
public enum SpeechwarpError: Error, Equatable {
    /// The sample rate is not 4000 to 384000, or the channel count is not 1 to 32.
    case unsupportedFormat
    /// The number of samples is not a whole number of frames.
    case partialFrame
    case outOfMemory
}

/// Speeds up speech. Write audio in, read the faster audio out.
///
/// Samples are interleaved: a frame is one sample per channel, and every count here is in frames unless it
/// says otherwise. Floats are in the range -1 to 1.
///
/// A stream is not thread safe. Use it from one thread, or lock around it.
public final class SpeechwarpStream {
    /// The slowest and fastest speeds that can be set.
    public static let speedRange: ClosedRange<Float> = 0.05...20

    /// The version of the C library, such as "0.3.7".
    public static var libraryVersion: String { String(cString: speechwarp_version()) }

    public let sampleRate: Int
    /// Samples in a frame.
    public let channels: Int

    private let stream: OpaquePointer

    /// Creates a stream at speed 1 with nonlinear speed-up on.
    public init(sampleRate: Int, channels: Int = 1) throws {
        guard (4000...384000).contains(sampleRate), (1...32).contains(channels) else {
            throw SpeechwarpError.unsupportedFormat
        }
        guard let stream = speechwarp_create(Int32(sampleRate), Int32(channels)) else {
            throw SpeechwarpError.outOfMemory
        }
        self.stream = stream
        self.sampleRate = sampleRate
        self.channels = channels
    }

    deinit {
        speechwarp_destroy(stream)
    }

    /// Overall speed: 2 plays twice as fast. Clamped to `speedRange`; zero, negative and NaN are ignored.
    ///
    /// Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
    /// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
    /// result within a few percent.
    public var speed: Float {
        get { speechwarp_get_speed(stream) }
        set { speechwarp_set_speed(stream, newValue) }
    }

    /// How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
    /// pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
    public var nonlinear: Float {
        get { speechwarp_get_nonlinear(stream) }
        set { speechwarp_set_nonlinear(stream, newValue) }
    }

    // Options for very high speeds (5x to 8x). All off by default; out-of-range values are clamped and NaN is
    // ignored, as for `nonlinear`. See docs/how-it-works.md.

    /// Shortens every pause to at most this many seconds of input before speeding up, so that the speed is spent
    /// on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1. `position` counts the
    /// frames left out.
    public var pauseCap: Float {
        get { speechwarp_get_pause_cap(stream) }
        set { speechwarp_set_pause_cap(stream, newValue) }
    }

    /// While the pause cap or rhythm is on, keeps the overall speed (true, the default): time saved in pauses is
    /// spent playing the words slower, never below 1x, and time spent in gaps is made up by playing them faster.
    /// When false, trimmed pauses make playback faster than `speed`.
    public var keepSpeed: Bool {
        get { speechwarp_get_keep_speed(stream) != 0 }
        set { speechwarp_set_keep_speed(stream, newValue ? 1 : 0) }
    }

    /// No 10 ms block of speech plays slower than this fraction of `speed`: at 8x, 0.5 keeps every block at 4x or
    /// more. 0 (the default) is off; 1 is the same as linear. Sensible: 0.3 to 0.7. Applies only with nonlinear
    /// speed-up above 1x.
    public var speedFloor: Float {
        get { speechwarp_get_speed_floor(stream) }
        set { speechwarp_set_speed_floor(stream, newValue) }
    }

    /// Seconds of silence put into the output `rhythmRate` times a second, at the quietest point nearby, which
    /// can help the listener keep up at very high speeds. 0 (the default) is off. Sensible: 0.02 to 0.06.
    /// Allowed: 0, or 0.005 to 0.2. `position` holds still during a gap.
    public var rhythmGap: Float {
        get { speechwarp_get_rhythm_gap(stream) }
        set { speechwarp_set_rhythm_gap(stream, newValue) }
    }

    /// Rhythm gaps a second of output, 1 to 16; default 5. Sensible: 4 to 8. Zero, negative and NaN are ignored.
    public var rhythmRate: Float {
        get { speechwarp_get_rhythm_rate(stream) }
        set { speechwarp_set_rhythm_rate(stream, newValue) }
    }

    // Options that follow the speed. Off by default; they set `pauseCap` and `speedFloor` again whenever the
    // speed changes. Setting the fixed option turns the rule off. NaN is ignored and other values are clamped.

    /// Heard pause: keeps each pause about `seconds` long in the output, by setting `pauseCap` to `seconds` times
    /// the current speed (clamped to 0.03 to 0.4 s of input) whenever the speed changes. Below `fromSpeed`
    /// pauses are left alone. `seconds`: 0 turns the rule off (and the pause cap with it); otherwise 0.002 to
    /// 0.4. `fromSpeed`: 1 to 20. Sensible: 0.015 to 0.06 heard, from 3x. Setting `pauseCap` turns the rule off
    /// (`heardPause` then reads 0); `pauseCap` reads the value in force.
    public func setHeardPause(_ seconds: Float, fromSpeed: Float) {
        speechwarp_set_heard_pause(stream, seconds, fromSpeed)
    }

    /// The heard pause last set with `setHeardPause`, in seconds; 0 if the rule is off.
    public var heardPause: Float { speechwarp_get_heard_pause(stream) }

    /// The speed from which `setHeardPause` applies, as last set.
    public var heardPauseFrom: Float { speechwarp_get_heard_pause_from(stream) }

    /// Floor blend: the speed floor in force is 0 below `fromSpeed`, rises linearly to `fraction` at `fullSpeed`,
    /// and stays at `fraction` above it, so that a speed ramp never changes the sound in a jump. `fraction`: 0
    /// turns the rule off (and the floor with it); otherwise up to 1. Speeds 1 to 20; if `fullSpeed` is not above
    /// `fromSpeed` it is taken as equal, and the floor steps to `fraction` at `fromSpeed`. Sensible: 0.5 from 4x,
    /// full at 6x. Setting `speedFloor` turns the rule off (`floorBlend` then reads 0); `speedFloor` reads the
    /// value in force.
    public func setFloorBlend(_ fraction: Float, fromSpeed: Float, fullSpeed: Float) {
        speechwarp_set_floor_blend(stream, fraction, fromSpeed, fullSpeed)
    }

    /// The floor fraction last set with `setFloorBlend`; 0 if the rule is off.
    public var floorBlend: Float { speechwarp_get_floor_blend(stream) }

    /// The speed from which the floor blend starts, as last set.
    public var floorBlendFrom: Float { speechwarp_get_floor_blend_from(stream) }

    /// The speed at which the floor blend reaches its full fraction, as last set.
    public var floorBlendFull: Float { speechwarp_get_floor_blend_full(stream) }

    /// Syllables a second in the input, pauses included, over about the last 60 s written; nil until 10 s have
    /// been written since creation or `reset()`. Multiply by the speed for the rate heard. An estimate, typically
    /// within about 10%.
    public var syllableRate: Double? {
        let rate = speechwarp_syllable_rate(stream)
        return rate < 0 ? nil : rate
    }

    /// Frames of output ready to read.
    public var framesAvailable: Int { Int(speechwarp_available(stream)) }

    /// The input frame, counted from creation or the last `reset()`, that the next output frame to be read was
    /// made from. This is how a player maps what is being heard back to a place in the source.
    ///
    /// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
    /// `flush()` has been read it equals the number of frames written.
    public var position: Int64 { speechwarp_position(stream) }

    /// Adds input. The output does not depend on how the input is divided between calls.
    public func write(_ samples: UnsafeBufferPointer<Float>) throws {
        let frames = try wholeFrames(samples.count)
        guard speechwarp_write(stream, samples.baseAddress, frames) != 0 else { throw SpeechwarpError.outOfMemory }
    }

    public func write(_ samples: UnsafeBufferPointer<Int16>) throws {
        let frames = try wholeFrames(samples.count)
        guard speechwarp_write_i16(stream, samples.baseAddress, frames) != 0 else { throw SpeechwarpError.outOfMemory }
    }

    public func write(_ samples: [Float]) throws {
        try samples.withUnsafeBufferPointer { try write($0) }
    }

    public func write(_ samples: [Int16]) throws {
        try samples.withUnsafeBufferPointer { try write($0) }
    }

    /// Takes processed output, as many whole frames as fit.
    ///
    /// - Returns: The number of frames written, which is the number of samples divided by `channels`. It may
    ///   be 0: output lags input by a short look-ahead.
    @discardableResult
    public func read(into samples: UnsafeMutableBufferPointer<Float>) -> Int {
        Int(speechwarp_read(stream, samples.baseAddress, Int32(clamping: samples.count / channels)))
    }

    @discardableResult
    public func read(into samples: UnsafeMutableBufferPointer<Int16>) -> Int {
        Int(speechwarp_read_i16(stream, samples.baseAddress, Int32(clamping: samples.count / channels)))
    }

    /// Takes all the output that is ready, or at most `maxFrames` frames of it.
    public func read(maxFrames: Int = .max) -> [Float] {
        let frames = min(maxFrames, framesAvailable)
        guard frames > 0 else { return [] }
        return [Float](unsafeUninitializedCapacity: frames * channels) { buffer, count in
            count = read(into: buffer) * channels
        }
    }

    /// Processes everything written so far, at the end of the input. Read until empty afterwards. Writing more
    /// starts a new stretch of audio, and `position` carries on counting.
    public func flush() throws {
        guard speechwarp_flush(stream) != 0 else { throw SpeechwarpError.outOfMemory }
    }

    /// Discards all buffered input and output, keeping the speed and nonlinear settings, and starts `position`
    /// again from zero. Use after seeking.
    public func reset() {
        speechwarp_reset(stream)
    }

    private func wholeFrames(_ samples: Int) throws -> Int32 {
        guard samples % channels == 0, let frames = Int32(exactly: samples / channels) else {
            throw SpeechwarpError.partialFrame
        }
        return frames
    }
}
