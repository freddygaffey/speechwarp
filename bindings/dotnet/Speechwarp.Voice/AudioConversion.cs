using System;
using AVFoundation;
using Foundation;

namespace Speechwarp.Voice;

/// <summary>
/// Converts a stream of audio buffers from one format to another, keeping the converter's state between buffers
/// so that resampling has no seams.
/// </summary>
internal sealed class AudioStreamConverter : IDisposable
{
    private readonly AVAudioConverter? _converter;

    public AVAudioFormat Input { get; }
    public AVAudioFormat Output { get; }

    public AudioStreamConverter(AVAudioFormat input, AVAudioFormat output)
    {
        Input = input;
        Output = output;
        if (!input.Equals(output))
        {
            _converter = new AVAudioConverter(input, output)
            {
                // Several input channels become one by mixing them, not by keeping the first.
                Downmix = true,
            };
        }
    }

    /// <summary>Converts <paramref name="buffer"/>; null when no output is ready yet.</summary>
    public AVAudioPcmBuffer? Convert(AVAudioPcmBuffer buffer)
    {
        if (_converter is null)
            return buffer.FrameLength > 0 ? buffer : null;
        return Run(_converter, buffer, endOfStream: false);
    }

    /// <summary>The frames still held back, at the end of the input.</summary>
    public AVAudioPcmBuffer? Flush() => _converter is null ? null : Run(_converter, null, endOfStream: true);

    private AVAudioPcmBuffer? Run(AVAudioConverter converter, AVAudioPcmBuffer? buffer, bool endOfStream)
    {
        var inputFrames = (double)(buffer?.FrameLength ?? 0);
        var capacity = (uint)(inputFrames * Output.SampleRate / Input.SampleRate) + 1024;
        var output = new AVAudioPcmBuffer(Output, capacity);
        var given = false;
        converter.ConvertToBuffer(output, out var error, (uint _, out AVAudioConverterInputStatus status) =>
        {
            if (!given && buffer is not null)
            {
                given = true;
                status = AVAudioConverterInputStatus.HaveData;
                return buffer;
            }
            status = endOfStream ? AVAudioConverterInputStatus.EndOfStream : AVAudioConverterInputStatus.NoDataNow;
            return null!;
        });
        if (error is not null || output.FrameLength == 0)
        {
            output.Dispose();
            return null;
        }
        return output;
    }

    public void Dispose() => _converter?.Dispose();
}

/// <summary>Moving samples between arrays and the system's buffers.</summary>
internal static class AudioSamples
{
    /// <summary>One channel of 32-bit float at <paramref name="sampleRate"/>, the format sessions take.</summary>
    public static AVAudioFormat MonoFormat(int sampleRate) =>
        new(AVAudioCommonFormat.PCMFloat32, sampleRate, 1, false);

    /// <summary>A buffer holding <paramref name="samples"/> in <paramref name="format"/> (from <see cref="MonoFormat"/>).</summary>
    public static unsafe AVAudioPcmBuffer? Buffer(ReadOnlySpan<float> samples, AVAudioFormat format)
    {
        if (samples.IsEmpty)
            return null;
        var buffer = new AVAudioPcmBuffer(format, (uint)samples.Length) { FrameLength = (uint)samples.Length };
        var channel = ((float**)buffer.FloatChannelData)[0];
        samples.CopyTo(new Span<float>(channel, samples.Length));
        return buffer;
    }

    /// <summary>The first channel as floats, whatever the buffer's sample format.</summary>
    public static unsafe float[] Mono(AVAudioPcmBuffer buffer)
    {
        var count = (int)buffer.FrameLength;
        var samples = new float[count];
        if (buffer.FloatChannelData != IntPtr.Zero)
        {
            new ReadOnlySpan<float>(((float**)buffer.FloatChannelData)[0], count).CopyTo(samples);
        }
        else if (buffer.Int16ChannelData != IntPtr.Zero)
        {
            var channel = ((short**)buffer.Int16ChannelData)[0];
            for (var i = 0; i < count; i++)
                samples[i] = channel[i] / 32768f;
        }
        else if (buffer.Int32ChannelData != IntPtr.Zero)
        {
            var channel = ((int**)buffer.Int32ChannelData)[0];
            for (var i = 0; i < count; i++)
                samples[i] = channel[i] / 2147483648f;
        }
        return samples;
    }

    /// <summary>Root mean square: 0 for silence, about 0.7 for a full-scale sine.</summary>
    public static float Rms(ReadOnlySpan<float> samples)
    {
        if (samples.IsEmpty)
            return 0;
        var sum = 0f;
        foreach (var sample in samples)
            sum += sample * sample;
        return MathF.Sqrt(sum / samples.Length);
    }

    /// <summary>Whether the app's Info.plist has <paramref name="key"/>.</summary>
    public static bool HasInfoKey(string key) => NSBundle.MainBundle.ObjectForInfoDictionary(key) is not null;
}
