using System;
using System.Threading.Tasks;
using AVFoundation;
using Foundation;
using Speechwarp.Transcription;

namespace Speechwarp.Voice;

/// <summary>
/// The default audio input, captured with AVAudioEngine, mixed to one channel and converted to float at
/// <see cref="SampleRate"/>, then given to a transcription session of any engine, or to a handler of your own.
/// </summary>
/// <remarks>
/// The app needs <c>NSMicrophoneUsageDescription</c> in its Info.plist. While running, the shared audio session is
/// set to play and record, unless <see cref="ConfiguresAudioSession"/> is false.
/// </remarks>
/// <example>
/// <code>
/// using var session = transcriber.StartSession(16000);
/// using var microphone = new MicrophoneSource(16000);
/// await microphone.StartAsync(session);
/// // ... show session.Partial, take session.TakeSegments() ...
/// microphone.Stop();
/// var rest = await session.FinishAsync();
/// </code>
/// </example>
public sealed class MicrophoneSource : IDisposable
{
    private readonly object _lock = new();
    private AVAudioEngine? _engine;
    private AudioStreamConverter? _converter;
    private float _level;

    /// <summary>A source delivering mono float at <paramref name="sampleRate"/> (16000 suits every engine here).</summary>
    public MicrophoneSource(int sampleRate = 16000)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sampleRate);
        SampleRate = sampleRate;
    }

    /// <summary>The rate samples are delivered at. Start the session at the same rate.</summary>
    public int SampleRate { get; }

    /// <summary>Whether <see cref="StartAsync(Action{float[]})"/> sets the shared audio session to play and record.</summary>
    public bool ConfiguresAudioSession { get; set; } = true;

    /// <summary>Whether it is capturing.</summary>
    public bool IsRunning
    {
        get { lock (_lock) return _engine is not null; }
    }

    /// <summary>
    /// The loudness of the latest audio, 0 to 1 (root mean square; ordinary speech is about 0.02 to 0.2), for a
    /// level meter. 0 when stopped.
    /// </summary>
    public float Level
    {
        get { lock (_lock) return _level; }
    }

    /// <summary>
    /// Asks for the microphone if the user has not yet been asked, and returns whether it may be used.
    /// </summary>
    /// <exception cref="InvalidOperationException">The Info.plist lacks NSMicrophoneUsageDescription (asking would end the app).</exception>
    public static Task<bool> RequestPermissionAsync()
    {
        const string key = "NSMicrophoneUsageDescription";
        if (!AudioSamples.HasInfoKey(key))
            throw new InvalidOperationException($"The app's Info.plist needs {key}.");
        var answer = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (OperatingSystem.IsIOSVersionAtLeast(17))
            AVAudioApplication.RequestRecordPermission(granted => answer.TrySetResult(granted));
        else
#pragma warning disable CA1422 // the replacement above needs iOS 17
            AVAudioSession.SharedInstance().RequestRecordPermission(granted => answer.TrySetResult(granted));
#pragma warning restore CA1422
        return answer.Task;
    }

    /// <summary>
    /// Asks for permission if needed and starts capturing into <paramref name="session"/>, which must have been
    /// started at <see cref="SampleRate"/>. Stopping does not finish the session; call its FinishAsync afterwards.
    /// </summary>
    public Task StartAsync(ITranscriptionSession session)
    {
        ArgumentNullException.ThrowIfNull(session);
        return StartAsync(samples => session.Write(samples));
    }

    /// <summary>
    /// Asks for permission if needed and starts capturing, calling <paramref name="handler"/> on an audio thread
    /// with each stretch of samples (mono, -1 to 1, at <see cref="SampleRate"/>, typically 0.1 s). Keep it quick.
    /// </summary>
    /// <exception cref="UnauthorizedAccessException">The user has refused the microphone.</exception>
    /// <exception cref="InvalidOperationException">Already running, or there is no input to capture.</exception>
    public async Task StartAsync(Action<float[]> handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        if (!await RequestPermissionAsync().ConfigureAwait(false))
            throw new UnauthorizedAccessException("The user has not allowed the microphone.");
        lock (_lock)
        {
            if (_engine is not null)
                throw new InvalidOperationException("The microphone is already running.");
            if (ConfiguresAudioSession)
            {
                var audioSession = AVAudioSession.SharedInstance();
                var error = audioSession.SetCategory(AVAudioSessionCategory.PlayAndRecord,
                    AVAudioSessionCategoryOptions.DefaultToSpeaker);
                error ??= audioSession.SetActive(true);
                if (error is not null)
                    throw new InvalidOperationException($"The audio session would not start: {error.LocalizedDescription}");
            }
            var engine = new AVAudioEngine();
            var input = engine.InputNode;
            var format = input.GetBusOutputFormat(0);
            if (format.SampleRate <= 0 || format.ChannelCount == 0)
                throw new InvalidOperationException("No audio input is available.");
            var converter = new AudioStreamConverter(format, AudioSamples.MonoFormat(SampleRate));
            input.InstallTapOnBus(0, (uint)(format.SampleRate / 10), format, (buffer, _) =>
            {
                using var converted = converter.Convert(buffer);
                if (converted is null)
                    return;
                var samples = AudioSamples.Mono(converted);
                lock (_lock)
                    _level = AudioSamples.Rms(samples);
                handler(samples);
            });
            engine.Prepare();
            if (!engine.StartAndReturnError(out var startError))
            {
                input.RemoveTapOnBus(0);
                converter.Dispose();
                engine.Dispose();
                throw new InvalidOperationException($"The microphone would not start: {startError?.LocalizedDescription}");
            }
            _engine = engine;
            _converter = converter;
        }
    }

    /// <summary>Stops capturing. Safe to call when not running.</summary>
    public void Stop()
    {
        AVAudioEngine? engine;
        AudioStreamConverter? converter;
        lock (_lock)
        {
            engine = _engine;
            converter = _converter;
            _engine = null;
            _converter = null;
            _level = 0;
        }
        if (engine is null)
            return;
        engine.InputNode.RemoveTapOnBus(0);
        engine.Stop();
        engine.Dispose();
        converter?.Dispose();
        if (ConfiguresAudioSession)
            AVAudioSession.SharedInstance().SetActive(false, AVAudioSessionSetActiveOptions.NotifyOthersOnDeactivation);
    }

    /// <summary>Stops capturing.</summary>
    public void Dispose() => Stop();
}
