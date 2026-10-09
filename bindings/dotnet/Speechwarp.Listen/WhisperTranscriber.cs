using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Speechwarp.Transcription;

namespace Speechwarp.Listen;

/// <summary>
/// Speech to text with whisper.cpp, on every platform the package carries, through the engine-independent
/// <see cref="ITranscriber"/> interface. Everything runs on the device; the app supplies the model file (see
/// <see cref="WhisperModels"/>).
/// </summary>
/// <remarks>
/// <para>
/// A passage given at once (a sentence said back for the trainer, a clip) goes to <see cref="TranscribeAsync"/>.
/// Anything long or live (a whole book decoded in chunks, a microphone) goes to a session from
/// <see cref="StartSession"/>: it recognises the audio in chunks of 20 to 30 s, each cut at the quietest moment so
/// that no word is split, on a thread of its own, so <see cref="ITranscriptionSession.Write"/> never waits.
/// </para>
/// <para>
/// Thread safe: any number of transcriptions and sessions may run at once on one transcriber, each with its own
/// working memory (tens of MB for small models, hundreds for large ones). Disposing the transcriber frees the model
/// once the last of them has finished or been disposed.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// using var transcriber = new WhisperTranscriber("/path/to/ggml-base.en.bin");
/// await transcriber.PrepareAsync();
/// var transcript = await transcriber.TranscribeAsync(samples, 44100);
/// Console.WriteLine(transcript.Text);
/// </code>
/// </example>
public sealed class WhisperTranscriber : ITranscriber
{
    private readonly object _lock = new();
    private ModelHandle? _model;
    private Task? _loading;
    private bool _disposed;

    /// <summary>
    /// A transcriber for the whisper.cpp model file at <paramref name="modelPath"/>. Nothing is loaded until
    /// <see cref="PrepareAsync"/>.
    /// </summary>
    /// <param name="modelPath">The model file, such as one of <see cref="WhisperModels.All"/> downloaded.</param>
    /// <param name="model">
    /// What the file is, for <see cref="Model"/>; by default the catalogue model with the same file name, or a
    /// description of the file (see <see cref="WhisperModels.ForFile"/>).
    /// </param>
    /// <param name="threads">CPU threads for transcriptions that do not set their own; 0 for the number of cores, at most 8.</param>
    /// <param name="useGpu">
    /// Use the GPU where the library has one (Metal, on Apple systems); elsewhere, and in the iOS simulator, this is
    /// ignored. Much faster on a Mac with Apple silicon.
    /// </param>
    /// <exception cref="ArgumentException"><paramref name="model"/> belongs to another engine.</exception>
    public WhisperTranscriber(string modelPath, TranscriptionModel? model = null, int threads = 0, bool useGpu = false)
    {
        ArgumentNullException.ThrowIfNull(modelPath);
        ArgumentOutOfRangeException.ThrowIfNegative(threads);
        if (model is not null && model.Engine != TranscriptionEngine.Whisper)
            throw new ArgumentException($"{model.Id} is not a whisper model.", nameof(model));
        ModelPath = modelPath;
        Model = model ?? WhisperModels.ForFile(modelPath);
        Threads = threads;
        UseGpu = useGpu;
    }

    /// <inheritdoc/>
    public TranscriptionModel Model { get; }

    /// <summary>The model file.</summary>
    public string ModelPath { get; }

    /// <summary>CPU threads for transcriptions that do not set their own; 0 lets the library choose.</summary>
    public int Threads { get; }

    /// <summary>Whether the GPU is used where there is one.</summary>
    public bool UseGpu { get; }

    /// <inheritdoc/>
    public bool IsReady
    {
        get { lock (_lock) return _model is not null; }
    }

    /// <summary>
    /// Whether the loaded model understands many languages (false for the ".en" models), or null before
    /// <see cref="PrepareAsync"/> has completed.
    /// </summary>
    public bool? IsMultilingual
    {
        get
        {
            var model = LoadedModel();
            return model is null ? null : Native.speechwarp_listen_model_multilingual(model) != 0;
        }
    }

    /// <summary>The version of the native library, the same as the package's, such as "0.3.7".</summary>
    public static unsafe string NativeVersion => Native.Text(Native.speechwarp_listen_version())!;

    /// <summary>The version of whisper.cpp in the native library, such as "1.9.5".</summary>
    public static unsafe string EngineVersion => Native.Text(Native.speechwarp_listen_engine_version())!;

    /// <summary>What the native library can use on this machine (CPU features, GPU), as one line, for diagnostics.</summary>
    public static unsafe string SystemInfo => Native.Text(Native.speechwarp_listen_system_info())!;

    /// <summary>
    /// Sends whisper.cpp's own log to standard error when true; it goes nowhere when false (the default). Applies
    /// to the whole process.
    /// </summary>
    public static void SetEngineLog(bool on) => Native.speechwarp_listen_set_log(on ? 1 : 0);

    /// <summary>
    /// Loads the model, on a background thread: from a fraction of a second (tiny) to several seconds (large).
    /// Calling it again once loaded does nothing. Progress goes from 0 to 1 with nothing between, as whisper.cpp does
    /// not report it. Cancelling stops the wait, not the load, which finishes in the background.
    /// </summary>
    /// <exception cref="FileNotFoundException">There is no file at <see cref="ModelPath"/>.</exception>
    /// <exception cref="InvalidDataException">The file is not a whisper.cpp model, or memory ran out.</exception>
    public async Task PrepareAsync(IProgress<double>? progress = null, CancellationToken cancellation = default)
    {
        Task loading;
        lock (_lock)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_model is null)
                _loading ??= Task.Run(Load);
            loading = _loading ?? Task.CompletedTask;
        }
        progress?.Report(0);
        await loading.WaitAsync(cancellation).ConfigureAwait(false);
        progress?.Report(1);
    }

    private void Load()
    {
        ModelHandle? model = null;
        Exception? failure = null;
        try
        {
            model = Open();
        }
        catch (Exception e)
        {
            failure = e;
        }
        lock (_lock)
        {
            // A failed load can be tried again; a successful one is not repeated, as _model is set.
            _loading = null;
            if (model is not null && !_disposed)
                _model = model;
            else
                model?.Dispose();
        }
        if (failure is not null)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Throw(failure);
    }

    private ModelHandle Open()
    {
        if (!File.Exists(ModelPath))
            throw new FileNotFoundException("The model file does not exist.", ModelPath);
        // whisper.cpp's GPU code fails in the iOS simulator (its own examples turn it off there), so the CPU is used.
        bool gpu = UseGpu && !(OperatingSystem.IsIOS() && Environment.GetEnvironmentVariable("SIMULATOR_UDID") is not null);
        var model = new ModelHandle(Native.speechwarp_listen_model_load(ModelPath, Threads, gpu ? Native.LoadGpu : 0));
        if (model.IsInvalid)
            throw new InvalidDataException($"{ModelPath} is not a whisper.cpp model, or memory ran out.");
        return model;
    }

    private ModelHandle? LoadedModel()
    {
        lock (_lock)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            return _model;
        }
    }

    /// <summary>
    /// Transcribes a whole passage given at once: a sentence said back, a clip. Prepares first if needed. Mono
    /// samples, -1 to 1, at any rate from 4000 to 384000 Hz; they must not change until the task completes. The
    /// audio and a 16 kHz copy are held in memory, so use a session for more than a few minutes.
    /// </summary>
    /// <exception cref="ArgumentException">Whisper does not know <see cref="TranscriptionOptions.Language"/>.</exception>
    /// <exception cref="OperationCanceledException"><paramref name="cancellation"/> was cancelled.</exception>
    public async Task<Transcript> TranscribeAsync(ReadOnlyMemory<float> samples, int sampleRate,
        TranscriptionOptions? options = null, CancellationToken cancellation = default)
    {
        CheckRate(sampleRate);
        options ??= new TranscriptionOptions();
        if (!IsReady)
            await PrepareAsync(null, cancellation).ConfigureAwait(false);
        var model = LoadedModel()!;
        if (samples.IsEmpty)
            return new Transcript([]);
        return await Task.Run(() => Transcribe(model, samples, sampleRate, options, cancellation), cancellation)
            .ConfigureAwait(false);
    }

    private static unsafe Transcript Transcribe(ModelHandle model, ReadOnlyMemory<float> samples, int sampleRate,
        TranscriptionOptions options, CancellationToken cancellation)
    {
        using var native = CreateOptions(options);
        using var pin = samples.Pin();
        int error = Native.Ok;
        nint result;
        // The registration is disposed (waiting for a running callback) before the options are freed.
        using (cancellation.Register(static state => Native.speechwarp_listen_options_cancel((OptionsHandle)state!),
                   native))
        {
            result = Native.speechwarp_listen_transcribe(model, (float*)pin.Pointer, samples.Length, sampleRate, native,
                &error);
        }
        using var handle = new ResultHandle(result);
        if (handle.IsInvalid)
        {
            if (error == Native.ErrorCancelled)
                throw new OperationCanceledException(cancellation);
            throw Native.Failure(error);
        }
        return new Transcript(Results.Segments(handle));
    }

    /// <summary>
    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone. Any length.
    /// Writing never waits; a thread of the session's own recognises each chunk of 20 to 30 s as it completes and
    /// raises <see cref="ITranscriptionSession.SegmentsReady"/>. Audio not yet recognised is held at 16 kHz (about
    /// 230 MB an hour), so a caller decoding a book faster than it is recognised should keep
    /// <c>SecondsWritten - SecondsRecognised</c> to a few minutes. <see cref="ITranscriptionSession.Partial"/> is
    /// worked out only while someone reads it, so a book costs nothing extra. Dispose the session when done.
    /// </summary>
    /// <exception cref="InvalidOperationException"><see cref="PrepareAsync"/> has not completed.</exception>
    /// <exception cref="ArgumentException">Whisper does not know <see cref="TranscriptionOptions.Language"/>.</exception>
    public ITranscriptionSession StartSession(int sampleRate, TranscriptionOptions? options = null)
    {
        CheckRate(sampleRate);
        var model = LoadedModel() ?? throw new InvalidOperationException("Call PrepareAsync first.");
        using var native = CreateOptions(options ?? new TranscriptionOptions());
        var session = new SessionHandle(Native.speechwarp_listen_session_create(model, sampleRate, native), model);
        if (session.IsInvalid)
        {
            session.Dispose();
            throw new OutOfMemoryException("The session could not be created.");
        }
        return new WhisperSession(session);
    }

    /// <summary>
    /// Frees the model once every transcription running on it has finished and every session on it has been
    /// disposed.
    /// </summary>
    public void Dispose()
    {
        ModelHandle? model;
        lock (_lock)
        {
            if (_disposed)
                return;
            _disposed = true;
            model = _model;
            _model = null;
        }
        model?.Dispose();
    }

    private static void CheckRate(int sampleRate)
    {
        if (sampleRate is < 4000 or > 384000)
            throw new ArgumentOutOfRangeException(nameof(sampleRate), sampleRate, "The sample rate must be 4000 to 384000 Hz.");
    }

    private static OptionsHandle CreateOptions(TranscriptionOptions options)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(options.Threads, nameof(options));
        var native = new OptionsHandle(Native.speechwarp_listen_options_create());
        if (native.IsInvalid)
            throw new OutOfMemoryException("Options could not be created.");
        try
        {
            if (Native.speechwarp_listen_options_set_language(native, options.Language) != Native.Ok)
                throw new ArgumentException($"Whisper does not know the language \"{options.Language}\".", nameof(options));
            Native.speechwarp_listen_options_set_word_timestamps(native, options.WordTimestamps ? 1 : 0);
            // Whisper takes the prompt as the text spoken just before, so a list of words separated by commas works.
            if (options.Hints.Count > 0)
                Native.speechwarp_listen_options_set_prompt(native, string.Join(", ", options.Hints));
            Native.speechwarp_listen_options_set_preset(native, (int)options.Preset);
            Native.speechwarp_listen_options_set_threads(native, options.Threads);
            return native;
        }
        catch
        {
            native.Dispose();
            throw;
        }
    }
}

/// <summary>Turns native results into the shared types.</summary>
internal static class Results
{
    public static unsafe List<TranscriptSegment> Segments(ResultHandle result)
    {
        int count = Native.speechwarp_listen_result_segment_count(result);
        var segments = new List<TranscriptSegment>(count);
        for (int s = 0; s < count; s++)
        {
            int wordCount = Native.speechwarp_listen_result_word_count(result, s);
            var words = new TranscriptWord[wordCount];
            for (int w = 0; w < wordCount; w++)
            {
                words[w] = new TranscriptWord(
                    Native.Text(Native.speechwarp_listen_result_word_text(result, s, w)) ?? "",
                    Native.speechwarp_listen_result_word_start(result, s, w),
                    Native.speechwarp_listen_result_word_end(result, s, w),
                    Native.speechwarp_listen_result_word_probability(result, s, w));
            }
            segments.Add(new TranscriptSegment(
                Native.Text(Native.speechwarp_listen_result_segment_text(result, s)) ?? "",
                Native.speechwarp_listen_result_segment_start(result, s),
                Native.speechwarp_listen_result_segment_end(result, s),
                words));
        }
        return segments;
    }

    /// <summary>Several segments as one, for a partial guess; null if there are none.</summary>
    public static TranscriptSegment? Merge(IReadOnlyList<TranscriptSegment> segments)
    {
        if (segments.Count == 0)
            return null;
        if (segments.Count == 1)
            return segments[0];
        var words = new List<TranscriptWord>();
        var texts = new List<string>(segments.Count);
        foreach (var segment in segments)
        {
            words.AddRange(segment.Words);
            texts.Add(segment.Text);
        }
        return new TranscriptSegment(string.Join(" ", texts), segments[0].Start, segments[^1].End, words);
    }
}
