using System;

namespace Speechwarp;

/// <summary>
/// Estimates the syllable rate of audio that does not go through a <see cref="SpeechwarpStream"/>: the same
/// estimator as <see cref="SpeechwarpStream.SyllableRate"/>, on its own. Given the same input as a stream it
/// reports the same rate as the stream does with a 60 s window and a 10 s minimum.
/// </summary>
/// <remarks>A counter is not thread safe. Use it from one thread, or lock around it.</remarks>
public sealed unsafe class SyllableCounter : IDisposable
{
    private readonly SyllablesHandle _handle;

    /// <summary>Creates a counter.</summary>
    /// <param name="sampleRate">Samples per second, 4000 to 384000.</param>
    /// <param name="channels">1 to 32.</param>
    /// <exception cref="ArgumentOutOfRangeException">The sample rate or channel count is not supported.</exception>
    /// <exception cref="OutOfMemoryException">The native counter could not be allocated.</exception>
    public SyllableCounter(int sampleRate, int channels)
    {
        if (sampleRate < 4000 || sampleRate > 384000)
            throw new ArgumentOutOfRangeException(nameof(sampleRate), sampleRate, "Must be from 4000 to 384000.");
        if (channels < 1 || channels > 32)
            throw new ArgumentOutOfRangeException(nameof(channels), channels, "Must be from 1 to 32.");

        nint counter = Native.speechwarp_syllables_create(sampleRate, channels);
        if (counter == 0)
            throw new OutOfMemoryException("speechwarp could not allocate a syllable counter.");
        _handle = new SyllablesHandle(counter);
        SampleRate = sampleRate;
        Channels = channels;
    }

    /// <summary>Samples per second.</summary>
    public int SampleRate { get; }

    /// <summary>Samples in a frame.</summary>
    public int Channels { get; }

    /// <summary>Adds audio. Float input is converted to 16-bit exactly as a stream converts it.</summary>
    /// <param name="samples">Interleaved samples, -1 to 1; the length must be a whole number of frames.</param>
    /// <exception cref="ArgumentException">The length is not a whole number of frames.</exception>
    public void Write(ReadOnlySpan<float> samples)
    {
        int frames = WholeFrames(samples.Length);
        fixed (float* p = samples)
            Check(Native.speechwarp_syllables_write(_handle, p, frames));
    }

    /// <inheritdoc cref="Write(ReadOnlySpan{float})"/>
    public void Write(ReadOnlySpan<short> samples)
    {
        int frames = WholeFrames(samples.Length);
        fixed (short* p = samples)
            Check(Native.speechwarp_syllables_write_i16(_handle, p, frames));
    }

    /// <summary>
    /// Syllables a second over the last <paramref name="windowSeconds"/> written (or all of it, if less), or null
    /// until <paramref name="minimumSeconds"/> have been written.
    /// </summary>
    /// <param name="windowSeconds">Clamped to 1 to 120.</param>
    /// <param name="minimumSeconds">Clamped to 0 to the window.</param>
    public double? Rate(double windowSeconds = 60, double minimumSeconds = 10)
    {
        double rate = Native.speechwarp_syllables_rate(_handle, windowSeconds, minimumSeconds);
        return rate < 0 ? null : rate;
    }

    /// <summary>Forgets everything written.</summary>
    public void Reset() => Native.speechwarp_syllables_reset(_handle);

    /// <summary>Frees the native counter.</summary>
    public void Dispose() => _handle.Dispose();

    private int WholeFrames(int samples)
    {
        if (samples % Channels != 0)
            throw new ArgumentException($"{samples} samples is not a whole number of {Channels}-channel frames.", "samples");
        return samples / Channels;
    }

    private static void Check(int result)
    {
        if (result == 0)
            throw new ArgumentException("speechwarp rejected the samples.");
    }
}
