using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Speechwarp.Transcription;

// The engine-independent face of speech-to-text. Engines implement ITranscriber: Apple's on-device recogniser
// (package Speechwarp.Voice, iOS) and whisper.cpp (package Speechwarp.Listen, every platform). An app codes
// against these types and picks an engine, or lets the user pick one, at run time.

/// <summary>Which engine a model belongs to.</summary>
public enum TranscriptionEngine
{
    /// <summary>Apple's on-device speech recogniser. The system manages its models.</summary>
    Apple,
    /// <summary>whisper.cpp. The app downloads the model file and passes its path.</summary>
    Whisper,
}

/// <summary>How to trade speed against accuracy, where an engine offers the choice.</summary>
public enum TranscriptionPreset
{
    /// <summary>Fastest: live microphone, quick checks.</summary>
    Fast,
    /// <summary>The default.</summary>
    Balanced,
    /// <summary>Slowest and most accurate: whole books, overnight.</summary>
    Accurate,
}

/// <summary>A model an engine can use.</summary>
/// <param name="Id">Stable identifier, such as "whisper-base.en" or "apple-en-US".</param>
/// <param name="Engine">The engine it belongs to.</param>
/// <param name="Name">A name to show, such as "Whisper base (English)".</param>
/// <param name="Languages">BCP 47 tags it understands; empty means many (it detects the language).</param>
/// <param name="SizeBytes">Download size, or 0 when the system manages it.</param>
/// <param name="DownloadUrl">Where the app can download it, or null when the system manages it.</param>
/// <param name="Sha256">Lower-case hex SHA-256 of the download, to check it, or null.</param>
/// <param name="RelativeSpeed">Rough speed against the engine's fastest model, 1 = fastest; for choosing.</param>
public sealed record TranscriptionModel(
    string Id,
    TranscriptionEngine Engine,
    string Name,
    IReadOnlyList<string> Languages,
    long SizeBytes,
    string? DownloadUrl,
    string? Sha256,
    double RelativeSpeed);

/// <summary>Settings for a transcription. Every field has a sensible default.</summary>
public sealed record TranscriptionOptions
{
    /// <summary>BCP 47 tag of the speech ("en", "en-US"), or null to detect it where the engine can.</summary>
    public string? Language { get; init; }
    /// <summary>Times for each word as well as each segment. Costs a little speed.</summary>
    public bool WordTimestamps { get; init; } = true;
    /// <summary>Words likely to appear (names, invented words) to help the recogniser. May be ignored.</summary>
    public IReadOnlyList<string> Hints { get; init; } = [];
    /// <summary>Speed against accuracy.</summary>
    public TranscriptionPreset Preset { get; init; } = TranscriptionPreset.Balanced;
    /// <summary>CPU threads, where the engine uses them; 0 lets it choose.</summary>
    public int Threads { get; init; }
}

/// <summary>A word with its time in the audio, in seconds from the start of what was given.</summary>
/// <param name="Text">The word.</param>
/// <param name="Start">When it starts, in seconds.</param>
/// <param name="End">When it ends, in seconds.</param>
/// <param name="Confidence">0 to 1, or NaN when the engine does not say.</param>
public sealed record TranscriptWord(string Text, double Start, double End, double Confidence);

/// <summary>A stretch of recognised speech, usually a phrase or sentence.</summary>
/// <param name="Text">What was said.</param>
/// <param name="Start">When it starts, in seconds.</param>
/// <param name="End">When it ends, in seconds.</param>
/// <param name="Words">Empty unless word timestamps were asked for and the engine gives them.</param>
public sealed record TranscriptSegment(string Text, double Start, double End, IReadOnlyList<TranscriptWord> Words);

/// <summary>What was recognised.</summary>
/// <param name="Segments">The segments, in order.</param>
public sealed record Transcript(IReadOnlyList<TranscriptSegment> Segments)
{
    /// <summary>All the segments' text, joined with spaces.</summary>
    public string Text => string.Join(" ", System.Linq.Enumerable.Select(Segments, s => s.Text.Trim()));
}

/// <summary>A speech recogniser. Thread safe unless an engine says otherwise.</summary>
public interface ITranscriber : IDisposable
{
    /// <summary>The model in use.</summary>
    TranscriptionModel Model { get; }

    /// <summary>Whether <see cref="PrepareAsync"/> has completed and transcription can start.</summary>
    bool IsReady { get; }

    /// <summary>
    /// Gets ready: loads the model, or for Apple asks permission and installs the system's assets if needed.
    /// <paramref name="progress"/> reports 0 to 1 where the engine knows.
    /// </summary>
    Task PrepareAsync(IProgress<double>? progress = null, CancellationToken cancellation = default);

    /// <summary>Transcribes a whole passage given at once: a sentence said back, a clip. Mono samples, -1 to 1.</summary>
    Task<Transcript> TranscribeAsync(ReadOnlyMemory<float> samples, int sampleRate, TranscriptionOptions? options = null,
        CancellationToken cancellation = default);

    /// <summary>
    /// Starts a session for audio that arrives over time: a book decoded in chunks, or a microphone. Any length.
    /// </summary>
    ITranscriptionSession StartSession(int sampleRate, TranscriptionOptions? options = null);
}

/// <summary>Audio in over time, recognised text out as it is ready.</summary>
public interface ITranscriptionSession : IDisposable
{
    /// <summary>Adds mono samples, -1 to 1, of any length. Never waits for recognition.</summary>
    void Write(ReadOnlySpan<float> samples);

    /// <summary>Segments finished since the last call, in order. Times are from the start of the session.</summary>
    IReadOnlyList<TranscriptSegment> TakeSegments();

    /// <summary>The engine's current guess at speech not yet finished, for live display, or null.</summary>
    TranscriptSegment? Partial { get; }

    /// <summary>Seconds of audio written so far, and recognised so far: their ratio is the progress.</summary>
    double SecondsWritten { get; }

    /// <summary>Seconds of the audio written that have been recognised.</summary>
    double SecondsRecognised { get; }

    /// <summary>Raised on a background thread whenever <see cref="TakeSegments"/> has something new.</summary>
    event Action? SegmentsReady;

    /// <summary>Ends the input and returns everything not yet taken.</summary>
    Task<Transcript> FinishAsync(CancellationToken cancellation = default);
}
