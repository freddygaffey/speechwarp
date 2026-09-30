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

    /// <summary>The version of the native library, such as "0.1.0".</summary>
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
