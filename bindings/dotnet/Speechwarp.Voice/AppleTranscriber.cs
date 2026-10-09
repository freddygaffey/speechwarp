using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using AVFoundation;
using Foundation;
using Speech;
using Speechwarp.Transcription;

namespace Speechwarp.Voice;

/// <summary>What went wrong with Apple's speech recogniser.</summary>
public enum AppleTranscriberError
{
    /// <summary>The user has refused speech recognition, or it is restricted on this device.</summary>
    NotAuthorised,
    /// <summary>The app's Info.plist lacks the usage description the system needs before it will ask the user.</summary>
    MissingUsageDescription,
    /// <summary>The system cannot recognise this language on the device.</summary>
    UnsupportedLanguage,
    /// <summary><see cref="AppleTranscriber.PrepareAsync"/> has not completed.</summary>
    NotPrepared,
    /// <summary>The recogniser failed; the message has the system's description.</summary>
    RecognitionFailed,
}

/// <summary>Thrown by <see cref="AppleTranscriber"/> and its sessions.</summary>
public sealed class AppleTranscriberException : Exception
{
    /// <summary>What went wrong.</summary>
    public AppleTranscriberError Error { get; }

    /// <summary>Creates the exception.</summary>
    public AppleTranscriberException(AppleTranscriberError error, string message) : base(message) => Error = error;
}

/// <summary>
/// Apple's on-device speech recogniser (SFSpeechRecognizer with on-device recognition required), through the
/// engine-independent <see cref="ITranscriber"/> interface. Nothing leaves the device.
/// </summary>
/// <remarks>
/// <para>
/// SFSpeechRecognizer is made for utterances, so long input (a book, a microphone left on) is given to it in
/// requests of 20 to 50 s, cut in pauses, recognised one after another; times are from the start of the session.
/// Apple's newer SpeechAnalyzer (iOS 26) is Swift-only and not bound for .NET, so this package does not use it;
/// the Swift package does.
/// </para>
/// <para>
/// The app needs <c>NSSpeechRecognitionUsageDescription</c> in its Info.plist (and, for the microphone,
/// <c>NSMicrophoneUsageDescription</c>; see <see cref="MicrophoneSource"/>).
/// </para>
/// </remarks>
/// <example>
/// <code>
/// using var transcriber = new AppleTranscriber(AppleTranscriber.ModelFor("en-GB"));
/// await transcriber.PrepareAsync();
/// var transcript = await transcriber.TranscribeAsync(samples, 16000);
/// </code>
/// </example>
public sealed class AppleTranscriber : ITranscriber
{
    private volatile bool _ready;

    /// <inheritdoc/>
    public TranscriptionModel Model { get; }

    /// <summary>The language the model is for.</summary>
    public string Language { get; }

    /// <inheritdoc/>
    public bool IsReady => _ready;

    /// <summary>A transcriber for <paramref name="model"/>, which must be one of Apple's.</summary>
    /// <exception cref="ArgumentException">The model belongs to another engine.</exception>
    public AppleTranscriber(TranscriptionModel model)
    {
        if (model.Engine != TranscriptionEngine.Apple)
            throw new ArgumentException($"{model.Id} is not one of Apple's models.", nameof(model));
        Model = model;
        Language = model.Languages.FirstOrDefault() ?? model.Id["apple-".Length..];
    }

    /// <summary>One model per language this device can recognise on the device.</summary>
    public static IReadOnlyList<TranscriptionModel> Models =>
        SFSpeechRecognizer.SupportedLocales.ToArray()
            .Where(locale => SupportsOnDevice(locale.Identifier))
            .Select(locale => ModelFor(locale.Identifier))
            .OrderBy(model => model.Id, StringComparer.Ordinal)
            .ToList();

    /// <summary>
    /// The model for a BCP 47 language tag ("en-GB"), whether or not this device supports it;
    /// <see cref="PrepareAsync"/> says.
    /// </summary>
    public static TranscriptionModel ModelFor(string language)
    {
        var tag = language.Replace('_', '-');
        string name;
        try
        {
            name = CultureInfo.GetCultureInfo(tag).EnglishName;
        }
        catch (CultureNotFoundException)
        {
            name = tag;
        }
        return new TranscriptionModel($"apple-{tag}", TranscriptionEngine.Apple, $"Apple on-device, {name}", [tag],
            0, null, null, 1);
    }

    private static bool SupportsOnDevice(string language)
    {
        using var recognizer = new SFSpeechRecognizer(new NSLocale(language));
        return recognizer.SupportsOnDeviceRecognition;
    }

    /// <summary>
    /// Asks for permission to recognise speech if it has not been given, and checks the device can recognise the
    /// language on the device. The system asks the user only once; a refusal throws
    /// <see cref="AppleTranscriberError.NotAuthorised"/>, and the user must then allow it in Settings. Progress
    /// goes straight to 1: the system manages the recogniser's models.
    /// </summary>
    public async Task PrepareAsync(IProgress<double>? progress = null, CancellationToken cancellation = default)
    {
        await AuthoriseAsync().WaitAsync(cancellation).ConfigureAwait(false);
        if (!SupportsOnDevice(Language))
            throw new AppleTranscriberException(AppleTranscriberError.UnsupportedLanguage,
                $"This device cannot recognise {Language} on the device.");
        _ready = true;
        progress?.Report(1);
    }

    private static async Task AuthoriseAsync()
    {
        switch (SFSpeechRecognizer.AuthorizationStatus)
        {
            case SFSpeechRecognizerAuthorizationStatus.Authorized:
                return;
            case SFSpeechRecognizerAuthorizationStatus.Denied:
            case SFSpeechRecognizerAuthorizationStatus.Restricted:
                throw new AppleTranscriberException(AppleTranscriberError.NotAuthorised,
                    "Speech recognition is not allowed for this app.");
        }
        // Asking without the usage description ends the app, so check for it first.
        const string key = "NSSpeechRecognitionUsageDescription";
        if (!AudioSamples.HasInfoKey(key))
            throw new AppleTranscriberException(AppleTranscriberError.MissingUsageDescription,
                $"The app's Info.plist needs {key}.");
        var answer = new TaskCompletionSource<SFSpeechRecognizerAuthorizationStatus>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        SFSpeechRecognizer.RequestAuthorization(status => answer.TrySetResult(status));
        if (await answer.Task.ConfigureAwait(false) != SFSpeechRecognizerAuthorizationStatus.Authorized)
            throw new AppleTranscriberException(AppleTranscriberError.NotAuthorised,
                "Speech recognition is not allowed for this app.");
    }

    /// <summary>
    /// Transcribes a whole passage given at once: a sentence said back, a clip. Prepares first if needed. Mono
    /// samples, -1 to 1, at any rate. The model fixes the language, so <see cref="TranscriptionOptions.Language"/>
    /// is not used.
    /// </summary>
    public async Task<Transcript> TranscribeAsync(ReadOnlyMemory<float> samples, int sampleRate,
        TranscriptionOptions? options = null, CancellationToken cancellation = default)
    {
        if (!IsReady)
            await PrepareAsync(null, cancellation).ConfigureAwait(false);
        using var session = StartSession(sampleRate, options);
        session.Write(samples.Span);
        return await session.FinishAsync(cancellation).ConfigureAwait(false);
    }

    /// <summary>
    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone (see
    /// <see cref="MicrophoneSource"/>). Writing never waits, so a caller decoding a book faster than it is
    /// recognised should keep <c>SecondsWritten - SecondsRecognised</c> to a few minutes, or memory grows with
    /// the backlog.
    /// </summary>
    /// <exception cref="AppleTranscriberException"><see cref="PrepareAsync"/> has not completed.</exception>
    public ITranscriptionSession StartSession(int sampleRate, TranscriptionOptions? options = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sampleRate);
        if (!IsReady)
            throw new AppleTranscriberException(AppleTranscriberError.NotPrepared, "Call PrepareAsync first.");
        return new RecognizerSession(new SFSpeechRecognizer(new NSLocale(Language)), sampleRate,
            options ?? new TranscriptionOptions());
    }

    /// <summary>Nothing to release: the system owns the recogniser.</summary>
    public void Dispose()
    {
    }
}

/// <summary>
/// A session on SFSpeechRecognizer, on the device. The audio is cut into requests (see <see cref="ChunkCutter"/>)
/// recognised one after another; the one being filled gets audio as it arrives, so a microphone sees partials.
/// </summary>
internal sealed class RecognizerSession : ITranscriptionSession
{
    private sealed class Chunk(int start)
    {
        public int Start { get; } = start;
        public List<float> Samples { get; } = [];
        public int Frames { get; set; }
        public bool Ended { get; set; }
        public bool Done { get; set; }
        public SFSpeechAudioBufferRecognitionRequest? Request { get; set; }
        public SFSpeechRecognitionTask? Task { get; set; }
    }

    private readonly object _lock = new();
    private readonly SFSpeechRecognizer _recognizer;
    private readonly int _sampleRate;
    private readonly TranscriptionOptions _options;
    private readonly AVAudioFormat _format;
    private readonly ChunkCutter _cutter;
    private readonly Queue<Chunk> _waiting = new();
    private readonly List<TranscriptSegment> _ready = [];
    private readonly TaskCompletionSource _finished = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private Chunk? _active;
    private Chunk? _lastWaiting;
    private TranscriptSegment? _partial;
    private long _framesWritten;
    private double _recognised;
    private int _framesCut;
    private bool _inputEnded;
    private bool _disposed;
    private string? _failure;

    public RecognizerSession(SFSpeechRecognizer recognizer, int sampleRate, TranscriptionOptions options)
    {
        _recognizer = recognizer;
        _recognizer.Queue = new NSOperationQueue { MaxConcurrentOperationCount = 1 };
        _sampleRate = sampleRate;
        _options = options;
        _format = AudioSamples.MonoFormat(sampleRate);
        _cutter = new ChunkCutter(sampleRate);
    }

    public event Action? SegmentsReady;

    public TranscriptSegment? Partial
    {
        get { lock (_lock) return _partial; }
    }

    public double SecondsWritten
    {
        get { lock (_lock) return (double)_framesWritten / _sampleRate; }
    }

    public double SecondsRecognised
    {
        get { lock (_lock) return Math.Min(_recognised, (double)_framesWritten / _sampleRate); }
    }

    public void Write(ReadOnlySpan<float> samples)
    {
        if (samples.IsEmpty)
            return;
        lock (_lock)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_inputEnded)
                throw new InvalidOperationException("The session has finished.");
            _framesWritten += samples.Length;
            foreach (var piece in _cutter.Push(samples))
                Handle(piece);
        }
    }

    public IReadOnlyList<TranscriptSegment> TakeSegments()
    {
        lock (_lock)
        {
            var taken = _ready.ToArray();
            _ready.Clear();
            return taken;
        }
    }

    public async Task<Transcript> FinishAsync(CancellationToken cancellation = default)
    {
        lock (_lock)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (!_inputEnded)
            {
                foreach (var piece in _cutter.Flush())
                    Handle(piece);
                Handle(ChunkCutter.Piece.Cut);
                _inputEnded = true;
                StartNext();
                CheckFinished();
            }
        }
        await _finished.Task.WaitAsync(cancellation).ConfigureAwait(false);
        lock (_lock)
        {
            if (_failure is not null)
                throw new AppleTranscriberException(AppleTranscriberError.RecognitionFailed, _failure);
            _partial = null;
            _recognised = (double)_framesWritten / _sampleRate;
        }
        return new Transcript(TakeSegments());
    }

    /// <summary>The request being filled: the last waiting one, or the active one, unless it has been ended.</summary>
    private Chunk? Filling =>
        _lastWaiting is { Ended: false } ? _lastWaiting
        : _waiting.Count == 0 && _active is { Ended: false } ? _active
        : null;

    private void Handle(ChunkCutter.Piece piece)
    {
        if (piece.Audio is { } audio)
        {
            var chunk = Filling;
            if (chunk is null)
            {
                chunk = new Chunk(_framesCut);
                _waiting.Enqueue(chunk);
                _lastWaiting = chunk;
            }
            chunk.Frames += audio.Length;
            if (chunk.Request is { } request)
            {
                using var buffer = AudioSamples.Buffer(audio, _format);
                if (buffer is not null)
                    request.Append(buffer);
            }
            else
            {
                chunk.Samples.AddRange(audio);
            }
            StartNext();
        }
        else if (Filling is { } chunk)
        {
            chunk.Ended = true;
            chunk.Request?.EndAudio();
            _framesCut = chunk.Start + chunk.Frames;
        }
    }

    /// <summary>Starts the next waiting request if none is running.</summary>
    private void StartNext()
    {
        if (_active is not null || _waiting.Count == 0)
            return;
        var chunk = _waiting.Dequeue();
        if (ReferenceEquals(chunk, _lastWaiting))
            _lastWaiting = null;
        _active = chunk;
        var request = new SFSpeechAudioBufferRecognitionRequest
        {
            RequiresOnDeviceRecognition = true,
            ShouldReportPartialResults = true,
            TaskHint = SFSpeechRecognitionTaskHint.Dictation,
            ContextualStrings = _options.Hints.ToArray(),
            AddsPunctuation = true,
        };
        chunk.Request = request;
        using (var buffer = AudioSamples.Buffer(chunk.Samples.ToArray(), _format))
        {
            if (buffer is not null)
                request.Append(buffer);
        }
        chunk.Samples.Clear();
        if (chunk.Ended)
            request.EndAudio();
        chunk.Task = _recognizer.GetRecognitionTask(request, (result, error) => Received(chunk, result, error));
    }

    private void Received(Chunk chunk, SFSpeechRecognitionResult? result, NSError? error)
    {
        Action? notify = null;
        lock (_lock)
        {
            if (chunk.Done || _disposed)
                return;
            var offset = (double)chunk.Start / _sampleRate;
            if (result is not null && !result.Final && error is null)
            {
                _partial = TranscriptAssembly.Segments(Words(result.BestTranscription), offset, double.PositiveInfinity,
                    double.PositiveInfinity, _options.WordTimestamps).FirstOrDefault();
                return;
            }
            chunk.Done = true;
            if (!chunk.Ended)
            {
                // The recogniser stopped before the request was ended (an error): later audio starts a new one.
                chunk.Ended = true;
                _framesCut = chunk.Start + chunk.Frames;
            }
            var end = (double)(chunk.Start + chunk.Frames) / _sampleRate;
            if (result is not null && result.Final)
            {
                // The recogniser can place the last word's end a little past the audio; keep it inside.
                var length = (double)chunk.Frames / _sampleRate;
                var words = Words(result.BestTranscription)
                    .Select(w => w with { Start = Math.Min(w.Start, length), End = Math.Min(w.End, length) });
                var segments = TranscriptAssembly.Segments(words, offset, keepWords: _options.WordTimestamps);
                _ready.AddRange(segments);
                if (segments.Count > 0)
                    notify = SegmentsReady;
            }
            else if (error is not null && !IsNoSpeech(error))
            {
                _failure ??= error.LocalizedDescription;
            }
            _recognised = Math.Max(_recognised, end);
            if (_partial is { } partial && partial.End <= end)
                _partial = null;
            chunk.Request?.Dispose();
            chunk.Task?.Dispose();
            chunk.Request = null;
            chunk.Task = null;
            _active = null;
            StartNext();
            CheckFinished();
        }
        notify?.Invoke();
    }

    private void CheckFinished()
    {
        if (_inputEnded && _active is null && _waiting.Count == 0)
            _finished.TrySetResult();
    }

    private static IEnumerable<TranscriptWord> Words(SFTranscription transcription) =>
        transcription.Segments.Select(s => new TranscriptWord(s.Substring, s.Timestamp, s.Timestamp + s.Duration,
            // Partial results give no confidence (0); say so rather than claim it is zero.
            s.Confidence > 0 ? s.Confidence : double.NaN));

    /// <summary>Whether the error only says a request held no speech, which is no failure for a stream with pauses.</summary>
    private static bool IsNoSpeech(NSError error) =>
        error.Domain == "kAFAssistantErrorDomain" && error.Code is 1110 or 203;

    /// <summary>Cancels recognition still under way.</summary>
    public void Dispose()
    {
        lock (_lock)
        {
            if (_disposed)
                return;
            _disposed = true;
            _active?.Task?.Cancel();
            _active = null;
            _waiting.Clear();
            _lastWaiting = null;
            _finished.TrySetResult();
        }
    }
}
