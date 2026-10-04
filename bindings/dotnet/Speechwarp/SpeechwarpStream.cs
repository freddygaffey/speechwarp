using System;
using System.Runtime.InteropServices;

namespace Speechwarp;

/// <summary>
/// Speeds up speech. Write audio in, read the faster audio out.
/// </summary>
/// <remarks>
/// <para>Samples are interleaved: a frame is one sample per channel, and every length and count here is in
/// frames unless it says otherwise. Floats are in the range -1 to 1.</para>
/// <para>A stream is not thread safe. Use it from one thread, or lock around it.</para>
/// </remarks>
public sealed unsafe class SpeechwarpStream : IDisposable
{
    /// <summary>The slowest speed that can be set.</summary>
    public const float MinSpeed = 0.05f;

    /// <summary>The fastest speed that can be set.</summary>
    public const float MaxSpeed = 20f;

    private readonly StreamHandle _handle;

    /// <summary>Creates a stream at speed 1 with nonlinear speed-up on.</summary>
    /// <param name="sampleRate">Samples per second, 4000 to 384000.</param>
    /// <param name="channels">1 to 32.</param>
    /// <exception cref="ArgumentOutOfRangeException">The sample rate or channel count is not supported.</exception>
    /// <exception cref="OutOfMemoryException">The native stream could not be allocated.</exception>
    public SpeechwarpStream(int sampleRate, int channels)
    {
        if (sampleRate < 4000 || sampleRate > 384000)
            throw new ArgumentOutOfRangeException(nameof(sampleRate), sampleRate, "Must be from 4000 to 384000.");
        if (channels < 1 || channels > 32)
            throw new ArgumentOutOfRangeException(nameof(channels), channels, "Must be from 1 to 32.");

        nint stream = Native.speechwarp_create(sampleRate, channels);
        if (stream == 0)
            throw new OutOfMemoryException("speechwarp could not allocate a stream.");
        _handle = new StreamHandle(stream);
        SampleRate = sampleRate;
        Channels = channels;
    }

    /// <summary>The version of the native library, such as "0.2.0".</summary>
    public static string NativeVersion => Marshal.PtrToStringUTF8((nint)Native.speechwarp_version())!;

    /// <summary>Samples per second.</summary>
    public int SampleRate { get; }

    /// <summary>Samples in a frame.</summary>
    public int Channels { get; }

    /// <summary>
    /// Overall speed: 2 plays twice as fast. Clamped to <see cref="MinSpeed"/>..<see cref="MaxSpeed"/>.
    /// </summary>
    /// <remarks>
    /// Takes effect on audio not yet processed, which includes the last 0.15 s or so written. With nonlinear
    /// speed-up the speed varies from moment to moment and its average is steered to this value; expect the
    /// result within a few percent.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">The value is zero, negative or not a number.</exception>
    public float Speed
    {
        get => Native.speechwarp_get_speed(_handle);
        set
        {
            if (!(value > 0))
                throw new ArgumentOutOfRangeException(nameof(value), value, "Must be greater than zero.");
            Native.speechwarp_set_speed(_handle, value);
        }
    }

    /// <summary>
    /// How unevenly time is compressed, 0 to 1. 1 (the default) slows consonants and hurries vowels and
    /// pauses, as a fast talker does. 0 compresses everything evenly. May be changed during playback.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a number.</exception>
    public float Nonlinear
    {
        get => Native.speechwarp_get_nonlinear(_handle);
        set
        {
            if (float.IsNaN(value))
                throw new ArgumentOutOfRangeException(nameof(value), value, "Must be a number.");
            Native.speechwarp_set_nonlinear(_handle, value);
        }
    }

    /// <summary>
    /// Pause cap, in seconds of input: every pause is shortened to at most this before speeding up, so that the
    /// speed is spent on words. 0 (the default) is off. Sensible: 0.04 to 0.2. Allowed: 0, or 0.01 to 1; other
    /// values are clamped. <see cref="Position"/> counts the frames left out.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a number.</exception>
    public float PauseCap
    {
        get => Native.speechwarp_get_pause_cap(_handle);
        set => Native.speechwarp_set_pause_cap(_handle, NotNaN(value));
    }

    /// <summary>
    /// Keep the overall speed while <see cref="PauseCap"/> or <see cref="RhythmGap"/> is on: time saved in pauses
    /// is spent playing the words slower, and time spent in gaps is made up by playing them faster, never below
    /// 1x. True by default. When false, trimmed pauses make playback faster than <see cref="Speed"/>.
    /// </summary>
    public bool KeepSpeed
    {
        get => Native.speechwarp_get_keep_speed(_handle) != 0;
        set => Native.speechwarp_set_keep_speed(_handle, value ? 1 : 0);
    }

    /// <summary>
    /// Speed floor, as a fraction of <see cref="Speed"/>: no 10 ms block of speech plays slower than this times
    /// the speed, so at 8x a floor of 0.5 keeps every block at 4x or more. 0 (the default) is off; 1 is the same
    /// as linear. Sensible: 0.3 to 0.7. Clamped to 0..1. Applies only with nonlinear speed-up above 1x.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a number.</exception>
    public float SpeedFloor
    {
        get => Native.speechwarp_get_speed_floor(_handle);
        set => Native.speechwarp_set_speed_floor(_handle, NotNaN(value));
    }

    /// <summary>
    /// Rhythm gap, in seconds: a short silence put into the output <see cref="RhythmRate"/> times a second, at the
    /// quietest point nearby, which can help the listener keep up at very high speeds. 0 (the default) is off.
    /// Sensible: 0.02 to 0.06. Allowed: 0, or 0.005 to 0.2; other values are clamped. <see cref="Position"/> holds
    /// still during a gap. Adds about 0.1 s of latency.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is not a number.</exception>
    public float RhythmGap
    {
        get => Native.speechwarp_get_rhythm_gap(_handle);
        set => Native.speechwarp_set_rhythm_gap(_handle, NotNaN(value));
    }

    /// <summary>Rhythm gaps a second of output, 1 to 16 (clamped); default 5. Sensible: 4 to 8.</summary>
    /// <exception cref="ArgumentOutOfRangeException">The value is zero, negative or not a number.</exception>
    public float RhythmRate
    {
        get => Native.speechwarp_get_rhythm_rate(_handle);
        set
        {
            if (!(value > 0))
                throw new ArgumentOutOfRangeException(nameof(value), value, "Must be greater than zero.");
            Native.speechwarp_set_rhythm_rate(_handle, value);
        }
    }

    /// <summary>
    /// Syllables a second in the input, pauses included, over about the last 60 s written; null until 10 s have
    /// been written since creation or <see cref="Reset"/>. Multiply by the speed for the rate heard. An estimate,
    /// typically within about 10%.
    /// </summary>
    public double? SyllableRate
    {
        get
        {
            var rate = Native.speechwarp_syllable_rate(_handle);
            return rate < 0 ? null : rate;
        }
    }

    private static float NotNaN(float value) =>
        float.IsNaN(value) ? throw new ArgumentOutOfRangeException(nameof(value), value, "Must be a number.") : value;

    /// <summary>Frames of output ready to read.</summary>
    public int FramesAvailable => Native.speechwarp_available(_handle);

    /// <summary>
    /// The input frame, counted from creation or the last <see cref="Reset"/>, that the next output frame to
    /// be read was made from. This is how a player maps what is being heard back to a place in the source.
    /// </summary>
    /// <remarks>
    /// It never goes backwards, it is approximate (within about 0.05 s of input), and once everything after a
    /// <see cref="Flush"/> has been read it equals the number of frames written.
    /// </remarks>
    public long Position => Native.speechwarp_position(_handle);

    /// <summary>Adds input. The output does not depend on how the input is divided between calls.</summary>
    /// <param name="samples">Interleaved samples; the length must be a whole number of frames.</param>
    /// <exception cref="ArgumentException">The length is not a whole number of frames.</exception>
    public void Write(ReadOnlySpan<float> samples)
    {
        int frames = WholeFrames(samples.Length, nameof(samples));
        fixed (float* p = samples)
            Check(Native.speechwarp_write(_handle, p, frames));
    }

    /// <inheritdoc cref="Write(ReadOnlySpan{float})"/>
    public void Write(ReadOnlySpan<short> samples)
    {
        int frames = WholeFrames(samples.Length, nameof(samples));
        fixed (short* p = samples)
            Check(Native.speechwarp_write_i16(_handle, p, frames));
    }

    /// <summary>Takes processed output.</summary>
    /// <param name="samples">Where to put it. As many whole frames as fit are written.</param>
    /// <returns>
    /// The number of frames written, which is the number of samples divided by <see cref="Channels"/>. It may
    /// be 0: output lags input by a short look-ahead.
    /// </returns>
    public int Read(Span<float> samples)
    {
        fixed (float* p = samples)
            return Native.speechwarp_read(_handle, p, samples.Length / Channels);
    }

    /// <inheritdoc cref="Read(Span{float})"/>
    public int Read(Span<short> samples)
    {
        fixed (short* p = samples)
            return Native.speechwarp_read_i16(_handle, p, samples.Length / Channels);
    }

    /// <summary>
    /// Processes everything written so far, at the end of the input. Read until empty afterwards. Writing more
    /// starts a new stretch of audio, and <see cref="Position"/> carries on counting.
    /// </summary>
    public void Flush() => Check(Native.speechwarp_flush(_handle));

    /// <summary>
    /// Discards all buffered input and output, keeping the speed and nonlinear settings, and starts
    /// <see cref="Position"/> again from zero. Use after seeking.
    /// </summary>
    public void Reset() => Native.speechwarp_reset(_handle);

    /// <summary>Frees the native stream.</summary>
    public void Dispose() => _handle.Dispose();

    private int WholeFrames(int samples, string name)
    {
        if (samples % Channels != 0)
            throw new ArgumentException($"{samples} samples is not a whole number of {Channels}-channel frames.", name);
        return samples / Channels;
    }

    private static void Check(int result)
    {
        if (result == 0)
            throw new OutOfMemoryException("speechwarp ran out of memory.");
    }
}
