using System.Diagnostics;
using System.Text.RegularExpressions;
using Speechwarp.Listen;
using Speechwarp.Transcription;

namespace Speechwarp.Listen.Tests;

/// <summary>
/// Tests of the binding. Those that recognise speech need a whisper.cpp model, named by the environment variable
/// SPEECHWARP_LISTEN_MODEL (ggml-tiny.en.bin is enough), and are skipped without one. The speech is the sample that
/// comes with whisper.cpp (third_party/whisper.cpp/samples/jfk.wav), so the submodule must be checked out.
/// </summary>
[TestClass]
public class ListenTests
{
    private const string Sentence = "ask not what your country can do for you ask what you can do for your country";

    private static readonly string Root =
        Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", ".."));

    private static string ModelPath()
    {
        var path = Environment.GetEnvironmentVariable("SPEECHWARP_LISTEN_MODEL");
        if (string.IsNullOrEmpty(path))
            Assert.Inconclusive("Set SPEECHWARP_LISTEN_MODEL to a whisper.cpp model to run this test.");
        Assert.IsTrue(File.Exists(path), $"SPEECHWARP_LISTEN_MODEL names a missing file: {path}");
        return path;
    }

    /// <summary>The whisper.cpp sample: 11 s of President Kennedy at 16 kHz.</summary>
    private static float[] Speech()
    {
        var path = Path.Combine(Root, "third_party", "whisper.cpp", "samples", "jfk.wav");
        if (!File.Exists(path))
            Assert.Inconclusive("The whisper.cpp submodule is not checked out.");
        var bytes = File.ReadAllBytes(path);
        // A plain 16-bit PCM WAV: find the "data" chunk.
        int at = 12;
        while (System.Text.Encoding.ASCII.GetString(bytes, at, 4) != "data")
            at += 8 + BitConverter.ToInt32(bytes, at + 4);
        int length = BitConverter.ToInt32(bytes, at + 4) / 2;
        var samples = new float[length];
        for (int i = 0; i < length; i++)
            samples[i] = BitConverter.ToInt16(bytes, at + 8 + 2 * i) / 32768f;
        return samples;
    }

    private static float[] Silence(double seconds) => new float[(int)(seconds * 16000)];

    private static float[] Join(params float[][] parts) => parts.SelectMany(part => part).ToArray();

    /// <summary>Lower case, no punctuation, single spaces.</summary>
    private static string Plain(string text) =>
        Regex.Replace(Regex.Replace(text.ToLowerInvariant(), @"[^a-z0-9' ]", " "), @"\s+", " ").Trim();

    private static int Count(string text, string phrase) => Regex.Matches(Plain(text), Regex.Escape(phrase)).Count;

    private static async Task<WhisperTranscriber> Prepared()
    {
        var transcriber = new WhisperTranscriber(ModelPath());
        await transcriber.PrepareAsync();
        return transcriber;
    }

    // ---- Without a model

    [TestMethod]
    public void NativeVersionMatchesTheHeaderAndThePackage()
    {
        string header = File.ReadAllText(Path.Combine(Root, "include", "speechwarp.h"));
        string project = File.ReadAllText(Path.Combine(Root, "bindings", "dotnet", "Speechwarp.Listen", "Speechwarp.Listen.csproj"));
        string inHeader = Regex.Match(header, "#define SPEECHWARP_VERSION \"(.*)\"").Groups[1].Value;
        string inProject = Regex.Match(project, "<Version>(.*)</Version>").Groups[1].Value;

        Assert.AreEqual(inHeader, WhisperTranscriber.NativeVersion);
        Assert.AreEqual(inHeader, inProject);
        StringAssert.Matches(WhisperTranscriber.EngineVersion, new Regex(@"^\d+\.\d+\.\d+"));
        Assert.IsFalse(string.IsNullOrWhiteSpace(WhisperTranscriber.SystemInfo));
    }

    [TestMethod]
    public void CatalogueDescribesEveryModel()
    {
        var models = WhisperModels.All;
        Assert.IsGreaterThanOrEqualTo(10, models.Count);
        Assert.AreEqual(models.Count, models.Select(model => model.Id).Distinct().Count());
        foreach (var model in models)
        {
            Assert.AreEqual(TranscriptionEngine.Whisper, model.Engine);
            StringAssert.StartsWith(model.Id, "whisper-");
            Assert.IsFalse(string.IsNullOrWhiteSpace(model.Name));
            Assert.IsGreaterThan(10_000_000L, model.SizeBytes);
            StringAssert.StartsWith(model.DownloadUrl, "https://");
            StringAssert.Matches(model.Sha256, new Regex("^[0-9a-f]{64}$"));
            Assert.IsGreaterThanOrEqualTo(1.0, model.RelativeSpeed);
            var fileName = WhisperModels.FileName(model);
            Assert.IsNotNull(fileName);
            StringAssert.EndsWith(model.DownloadUrl, "/" + fileName);
        }

        var tiny = WhisperModels.Find("whisper-tiny.en");
        Assert.IsNotNull(tiny);
        CollectionAssert.AreEqual(new[] { "en" }, tiny.Languages.ToArray());
        Assert.AreEqual("ggml-tiny.en.bin", WhisperModels.FileName(tiny));
        Assert.AreEqual(1.0, tiny.RelativeSpeed);
        Assert.IsEmpty(WhisperModels.Find("whisper-tiny")!.Languages); // multilingual
        Assert.IsNull(WhisperModels.Find("whisper-enormous"));
    }

    [TestMethod]
    public void AModelFileIsMatchedToTheCatalogueByName()
    {
        Assert.AreEqual("whisper-base.en", WhisperModels.ForFile("/models/ggml-base.en.bin").Id);

        var other = WhisperModels.ForFile("/models/my-own-model.bin");
        Assert.AreEqual("whisper-file:my-own-model.bin", other.Id);
        Assert.AreEqual(TranscriptionEngine.Whisper, other.Engine);
        Assert.IsNull(other.DownloadUrl);
        Assert.IsNull(WhisperModels.FileName(other));
        Assert.IsTrue(double.IsNaN(other.RelativeSpeed));

        using var transcriber = new WhisperTranscriber("/models/ggml-small.en.bin");
        Assert.AreEqual("whisper-small.en", transcriber.Model.Id);
        Assert.IsFalse(transcriber.IsReady);
        Assert.IsNull(transcriber.IsMultilingual);
    }

    [TestMethod]
    public void RefusesAnotherEnginesModel()
    {
        var apple = new TranscriptionModel("apple-en-GB", TranscriptionEngine.Apple, "Apple", ["en-GB"], 0, null, null, 1);
        Assert.ThrowsExactly<ArgumentException>(() => new WhisperTranscriber("model.bin", apple));
    }

    [TestMethod]
    public async Task ReportsAMissingOrBrokenModelFile()
    {
        using var missing = new WhisperTranscriber(Path.Combine(Path.GetTempPath(), "no-such-model.bin"));
        await Assert.ThrowsExactlyAsync<FileNotFoundException>(() => missing.PrepareAsync());
        Assert.IsFalse(missing.IsReady);
        Assert.ThrowsExactly<InvalidOperationException>(() => missing.StartSession(16000));

        var junk = Path.GetTempFileName();
        try
        {
            File.WriteAllText(junk, "not a model");
            using var broken = new WhisperTranscriber(junk);
            await Assert.ThrowsExactlyAsync<InvalidDataException>(() => broken.PrepareAsync());
            Assert.IsFalse(broken.IsReady);
        }
        finally
        {
            File.Delete(junk);
        }
    }

    // ---- With a model

    [TestMethod]
    public async Task TranscribesASentenceWithWordTimes()
    {
        using var transcriber = await Prepared();
        Assert.IsTrue(transcriber.IsReady);
        Assert.IsNotNull(transcriber.IsMultilingual);
        var progress = new List<double>();
        await transcriber.PrepareAsync(new SynchronousProgress(progress.Add)); // already loaded: nothing to do
        CollectionAssert.Contains(progress, 1.0);

        var transcript = await transcriber.TranscribeAsync(Speech(), 16000, new TranscriptionOptions { Language = "en" });

        StringAssert.Contains(Plain(transcript.Text), "ask not what your country can do for you");
        double previous = 0;
        foreach (var segment in transcript.Segments)
        {
            Assert.IsGreaterThanOrEqualTo(previous - 0.01, segment.Start);
            Assert.IsGreaterThanOrEqualTo(segment.Start, segment.End);
            Assert.IsLessThanOrEqualTo(11.5, segment.End);
            Assert.IsNotEmpty(segment.Words);
            foreach (var word in segment.Words)
            {
                Assert.IsGreaterThanOrEqualTo(segment.Start - 0.01, word.Start);
                Assert.IsLessThanOrEqualTo(segment.End + 0.01, word.End);
                Assert.IsTrue(word.Confidence is >= 0 and <= 1, $"confidence {word.Confidence}");
            }
            previous = segment.Start;
        }
        // "country" is said twice, the second time after 8 s.
        var country = transcript.Segments.SelectMany(s => s.Words).Where(w => Plain(w.Text) == "country").ToList();
        Assert.HasCount(2, country);
        Assert.IsGreaterThan(7.0, country[1].Start);

        var noWords = await transcriber.TranscribeAsync(Speech(), 16000, new TranscriptionOptions { WordTimestamps = false });
        Assert.IsTrue(noWords.Segments.All(segment => segment.Words.Count == 0));
        StringAssert.Contains(Plain(noWords.Text), "your country");
    }

    [TestMethod]
    public async Task TranscribesAtAnotherRate()
    {
        using var transcriber = await Prepared();
        var speech = Speech();
        var at48k = new float[speech.Length * 3];
        for (int i = 0; i < at48k.Length; i++)
        {
            // Linear interpolation from 16 to 48 kHz.
            int j = i / 3;
            float next = j + 1 < speech.Length ? speech[j + 1] : speech[j];
            at48k[i] = speech[j] + (next - speech[j]) * (i % 3) / 3f;
        }
        var transcript = await transcriber.TranscribeAsync(at48k, 48000);
        StringAssert.Contains(Plain(transcript.Text), "what your country can do");
        Assert.IsLessThanOrEqualTo(11.5, transcript.Segments[^1].End);
    }

    [TestMethod]
    public async Task SilenceAndNothingGiveNoSegments()
    {
        using var transcriber = await Prepared();
        Assert.IsEmpty((await transcriber.TranscribeAsync(Silence(3), 16000)).Segments);
        Assert.IsEmpty((await transcriber.TranscribeAsync(ReadOnlyMemory<float>.Empty, 16000)).Segments);
        await Assert.ThrowsExactlyAsync<ArgumentOutOfRangeException>(() => transcriber.TranscribeAsync(Silence(1), 1000));
    }

    [TestMethod]
    public async Task RefusesAnUnknownLanguage()
    {
        using var transcriber = await Prepared();
        var options = new TranscriptionOptions { Language = "qq" };
        await Assert.ThrowsExactlyAsync<ArgumentException>(() => transcriber.TranscribeAsync(Silence(1), 16000, options));
        Assert.ThrowsExactly<ArgumentException>(() => transcriber.StartSession(16000, options));
    }

    [TestMethod]
    public async Task CancelsATranscription()
    {
        using var transcriber = await Prepared();
        // Read outside Assert.ThrowsAsync, which would report a missing submodule as a failure, not a skip.
        var speech = Speech();
        using (var cancelled = new CancellationTokenSource())
        {
            cancelled.Cancel();
            await Assert.ThrowsAsync<OperationCanceledException>(() =>
                transcriber.TranscribeAsync(speech, 16000, cancellation: cancelled.Token));
        }

        // Four minutes of speech, cancelled almost at once: it stops long before it could have finished.
        var fourMinutes = Join(Enumerable.Repeat(Join(speech, Silence(1)), 20).ToArray());
        using var soon = new CancellationTokenSource(TimeSpan.FromMilliseconds(200));
        var clock = Stopwatch.StartNew();
        await Assert.ThrowsAsync<OperationCanceledException>(() =>
            transcriber.TranscribeAsync(fourMinutes, 16000, cancellation: soon.Token));
        var cancelledAfter = clock.Elapsed;

        // The transcriber is still usable.
        var transcript = await transcriber.TranscribeAsync(speech, 16000);
        StringAssert.Contains(Plain(transcript.Text), "your country");
        Assert.IsLessThan(TimeSpan.FromSeconds(10), cancelledAfter);
    }

    [TestMethod]
    public async Task ASessionRecognisesInChunksAsTheAudioArrives()
    {
        using var transcriber = await Prepared();
        // About 75 s: the sentence three times, apart, so the session must cut at least two chunks.
        var audio = Join(Speech(), Silence(14), Speech(), Silence(14), Speech(), Silence(14));
        using var session = transcriber.StartSession(16000, new TranscriptionOptions { Language = "en" });
        var taken = new List<TranscriptSegment>();
        int events = 0;
        session.SegmentsReady += () =>
        {
            Interlocked.Increment(ref events);
            lock (taken)
                taken.AddRange(session.TakeSegments());
        };

        // Written in 100 ms pieces as a microphone would, but as fast as possible.
        for (int at = 0; at < audio.Length; at += 1600)
            session.Write(audio.AsSpan(at, Math.Min(1600, audio.Length - at)));
        Assert.AreEqual(audio.Length / 16000.0, session.SecondsWritten, 1e-6);

        // Recognition happens on the session's own thread: wait for the first chunk.
        var clock = Stopwatch.StartNew();
        while (Volatile.Read(ref events) == 0 && clock.Elapsed < TimeSpan.FromSeconds(60))
            await Task.Delay(50);
        Assert.IsGreaterThan(0, Volatile.Read(ref events), "SegmentsReady was never raised before FinishAsync");
        Assert.IsGreaterThan(0.0, session.SecondsRecognised);

        var rest = await session.FinishAsync();
        List<TranscriptSegment> all;
        lock (taken)
            all = [.. taken, .. rest.Segments];
        Assert.AreEqual(session.SecondsWritten, session.SecondsRecognised, 0.01);
        Assert.IsEmpty(session.TakeSegments());
        Assert.ThrowsExactly<InvalidOperationException>(() => session.Write(Silence(1)));

        var text = string.Join(" ", all.Select(segment => segment.Text));
        for (int i = 1; i < all.Count; i++)
            Assert.IsGreaterThanOrEqualTo(all[i - 1].Start - 0.01, all[i].Start);
        // The sentence is said at 0, 25 and 50 s, and each time is recognised, at least in part: with the text before
        // as context, whisper's smallest models tend to shorten a sentence repeated word for word, and to place it
        // a few seconds early.
        Assert.IsGreaterThanOrEqualTo(3, Count(text, "do for your country"), text);
        Assert.IsLessThan(1.0, all[0].Start);
        Assert.IsGreaterThan(55.0, all[^1].End);
        Assert.IsLessThanOrEqualTo(audio.Length / 16000.0 + 0.01, all[^1].End);
    }

    [TestMethod]
    public async Task ASessionGivesAPartialGuessWhileItIsRead()
    {
        using var transcriber = await Prepared();
        using var session = transcriber.StartSession(16000, new TranscriptionOptions { Preset = TranscriptionPreset.Fast });
        Assert.IsNull(session.Partial);
        var speech = Speech();
        for (int at = 0; at < speech.Length; at += 1600)
        {
            session.Write(speech.AsSpan(at, Math.Min(1600, speech.Length - at)));
            _ = session.Partial;
        }
        TranscriptSegment? guess = null;
        var clock = Stopwatch.StartNew();
        while (clock.Elapsed < TimeSpan.FromSeconds(30))
        {
            guess = session.Partial;
            if (guess is not null && Plain(guess.Text).Contains("your country"))
                break;
            await Task.Delay(50);
        }
        Assert.IsNotNull(guess);
        StringAssert.Contains(Plain(guess.Text), "your country");
        Assert.IsEmpty(session.TakeSegments()); // a guess is not a finished segment

        var transcript = await session.FinishAsync();
        StringAssert.Contains(Plain(transcript.Text), "ask not what your country can do for you");
        Assert.IsNull(session.Partial);
    }

    [TestMethod]
    public async Task FinishingCanBeCancelledAndCarriedOn()
    {
        using var transcriber = await Prepared();
        using var session = transcriber.StartSession(16000);
        var audio = Join(Enumerable.Repeat(Join(Speech(), Silence(1)), 8).ToArray()); // 96 s
        session.Write(audio);
        using (var soon = new CancellationTokenSource(TimeSpan.FromMilliseconds(100)))
            await Assert.ThrowsAsync<OperationCanceledException>(() => session.FinishAsync(soon.Token));

        var transcript = await session.FinishAsync();
        var text = string.Join(" ", session.TakeSegments().Concat(transcript.Segments).Select(s => s.Text));
        Assert.IsGreaterThanOrEqualTo(8, Count(text, "do for your country"), text);
    }

    [TestMethod]
    public async Task DisposingASessionStopsItPromptly()
    {
        using var transcriber = await Prepared();
        var session = transcriber.StartSession(16000);
        session.Write(Join(Enumerable.Repeat(Join(Speech(), Silence(1)), 10).ToArray()));
        var finishing = session.FinishAsync();
        var clock = Stopwatch.StartNew();
        session.Dispose();
        Assert.IsLessThan(TimeSpan.FromSeconds(10), clock.Elapsed);
        await Assert.ThrowsAsync<Exception>(() => finishing);
        Assert.ThrowsExactly<ObjectDisposedException>(() => session.Write(Silence(1)));
        session.Dispose(); // twice is fine

        // The transcriber can be disposed while a session on it lives; the model goes when the session does.
        var transcriber2 = await Prepared();
        var orphan = transcriber2.StartSession(16000);
        transcriber2.Dispose();
        orphan.Write(Speech());
        var transcript = await orphan.FinishAsync();
        StringAssert.Contains(Plain(transcript.Text), "your country");
        orphan.Dispose();
    }

    /// <summary>Progress that reports at once, on the reporting thread.</summary>
    private sealed class SynchronousProgress(Action<double> report) : IProgress<double>
    {
        public void Report(double value) => report(value);
    }
}
