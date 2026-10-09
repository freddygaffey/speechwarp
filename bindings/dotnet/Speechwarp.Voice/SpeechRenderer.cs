using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using AVFoundation;
using CoreFoundation;
using Foundation;

namespace Speechwarp.Voice;

/// <summary>Mono audio rendered from text, with the sample rate the voice produced it at.</summary>
/// <param name="Samples">Samples from -1 to 1, one channel.</param>
/// <param name="SampleRate">Frames a second.</param>
public sealed record RenderedSpeech(float[] Samples, int SampleRate)
{
    /// <summary>The length in seconds.</summary>
    public double Duration => (double)Samples.Length / SampleRate;
}

/// <summary>
/// Turns text into audio with a system voice, into memory instead of to the speaker.
/// </summary>
/// <remarks>
/// The system delivers the audio on the main thread, so it must not be blocked waiting for a render (await it).
/// </remarks>
public sealed class SpeechRenderer : IDisposable
{
    private readonly AVSpeechSynthesizer _synthesizer = new();
    private readonly AVSpeechSynthesisVoice _voice;

    /// <summary>The voice this renders with.</summary>
    public SpeechVoice Voice { get; }

    /// <summary>Creates a renderer for <paramref name="voice"/>.</summary>
    /// <exception cref="ArgumentException">The voice is not installed.</exception>
    public SpeechRenderer(SpeechVoice voice)
    {
        _voice = AVSpeechSynthesisVoice.FromIdentifier(voice.Identifier)
            ?? throw new ArgumentException($"The voice {voice.Identifier} is not installed.", nameof(voice));
        Voice = voice;
    }

    /// <summary>
    /// Renders <paramref name="text"/> at the system <paramref name="rate"/> (see <see cref="SpeechRate"/>;
    /// default <see cref="SpeechRate.Normal"/>). Whitespace-only text gives no samples.
    /// </summary>
    public Task<RenderedSpeech> RenderAsync(string text, float? rate = null)
    {
        if (string.IsNullOrWhiteSpace(text))
            return Task.FromResult(new RenderedSpeech([], 22050));
        var utterance = new AVSpeechUtterance(text)
        {
            Voice = _voice,
            Rate = Math.Clamp(rate ?? SpeechRate.Normal, SpeechRate.Minimum, SpeechRate.Maximum),
        };
        var done = new TaskCompletionSource<RenderedSpeech>(TaskCreationOptions.RunContinuationsAsynchronously);
        var samples = new List<float>();
        var sampleRate = 0;
        var finished = false;
        void Start() => _synthesizer.WriteUtterance(utterance, buffer =>
        {
            if (finished || buffer is not AVAudioPcmBuffer pcm)
                return;
            if (pcm.FrameLength == 0)
            {
                finished = true;
                if (sampleRate == 0)
                    done.TrySetException(new InvalidOperationException("The system produced no audio for the text."));
                else
                    done.TrySetResult(new RenderedSpeech(samples.ToArray(), sampleRate));
                return;
            }
            sampleRate = (int)pcm.Format.SampleRate;
            AppendMono(pcm, samples);
        });
        if (NSThread.IsMain)
            Start();
        else
            DispatchQueue.MainQueue.DispatchAsync(Start);
        return done.Task;
    }

    /// <summary>The first channel as floats, whatever format the voice delivered.</summary>
    private static unsafe void AppendMono(AVAudioPcmBuffer pcm, List<float> into)
    {
        var count = (int)pcm.FrameLength;
        if (pcm.FloatChannelData != IntPtr.Zero)
        {
            var channel = ((float**)pcm.FloatChannelData)[0];
            into.AddRange(new ReadOnlySpan<float>(channel, count));
        }
        else if (pcm.Int16ChannelData != IntPtr.Zero)
        {
            var channel = ((short**)pcm.Int16ChannelData)[0];
            for (var i = 0; i < count; i++)
                into.Add(channel[i] / 32768f);
        }
        else if (pcm.Int32ChannelData != IntPtr.Zero)
        {
            var channel = ((int**)pcm.Int32ChannelData)[0];
            for (var i = 0; i < count; i++)
                into.Add(channel[i] / 2147483648f);
        }
    }

    /// <summary>Releases the system synthesiser.</summary>
    public void Dispose() => _synthesizer.Dispose();
}
