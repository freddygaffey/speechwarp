using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using AVFoundation;
using Foundation;
using NaturalLanguage;

namespace Speechwarp.Voice;

/// <summary>
/// A text read aloud by a system voice and sped up by speechwarp, rendered sentence by sentence a little ahead of
/// playback.
/// </summary>
/// <example>
/// <code>
/// var voice = SpeechVoice.Eloquence("en-US").First(v => v.Name == "Reed");
/// using var book = new SpokenText(text, voice) { Speed = 5 };     // on top of the voice's own rate
/// book.Configure(s => s.SetHeardPause(0.03f, 3));
/// // on the audio thread:
/// int frames = book.Read(buffer);                                // mono, book.SampleRate
/// // for the display:
/// int at = book.CharacterPosition;                               // index into the text
/// </code>
/// </example>
/// <remarks>
/// Rendering runs on the main thread (where the system delivers speech); everything else is safe from any
/// thread. <see cref="Read"/> never waits: if the next sentence is not rendered yet it returns what there is, and
/// <see cref="IsBuffering"/> says so. Character positions are indices into the string (UTF-16), as everywhere in
/// .NET.
/// </remarks>
public sealed class SpokenText : IDisposable
{
    /// <summary>A sentence: where it starts in the text, how long it is, and its text.</summary>
    public sealed record Sentence(int Start, int Length, string Text);

    private const int MaxSentence = 400;

    private readonly object _lock = new();
    private readonly SpeechRenderer _renderer;
    private readonly SpeechwarpStream _stream;
    private readonly Dictionary<int, float[]> _cache = [];
    private readonly Dictionary<int, bool> _rendered = []; // false while rendering
    private readonly List<(long InputStart, int Sentence, int Frames)> _segments = [];
    private float _rate;
    private double _lookahead = 30;
    private int _feeding; // the sentence being written into the stream
    private int _fed; // frames of it written
    private long _written; // frames written into the stream since the last seek
    private bool _flushed;
    private bool _buffering;
    private bool _renderingNow;
    private Exception? _failure;
    private bool _disposed;

    /// <summary>The text being read.</summary>
    public string Text { get; }
    /// <summary>The voice reading it.</summary>
    public SpeechVoice Voice { get; }
    /// <summary>The text cut into sentences.</summary>
    public IReadOnlyList<Sentence> Sentences { get; }
    /// <summary>The sample rate of what <see cref="Read"/> returns. Mono.</summary>
    public int SampleRate { get; }

    /// <summary>
    /// Splits <paramref name="text"/> into sentences and starts rendering the first ones.
    /// <paramref name="voiceRate"/> is the system rate (see <see cref="SpeechRate"/>); <see cref="Speed"/>
    /// multiplies it.
    /// </summary>
    public SpokenText(string text, SpeechVoice voice, float? voiceRate = null)
    {
        Text = text;
        Voice = voice;
        _renderer = new SpeechRenderer(voice);
        Sentences = Split(text);
        _rate = voiceRate ?? SpeechRate.Normal;
        var settings = AVSpeechSynthesisVoice.FromIdentifier(voice.Identifier)?.AudioFileSettings;
        SampleRate = settings?[AVAudioSettings.AVSampleRateKey] is NSNumber number ? number.Int32Value : 22050;
        _stream = new SpeechwarpStream(SampleRate, 1);
        ScheduleRendering();
    }

    /// <summary>The system rate the voice speaks at. Applies to sentences rendered afterwards.</summary>
    public float VoiceRate
    {
        get { lock (_lock) return _rate; }
        set
        {
            lock (_lock)
            {
                _rate = value;
                _cache.Clear();
                _rendered.Clear();
            }
            ScheduleRendering();
        }
    }

    /// <summary>speechwarp's speed on top of the voice's own rate: 2 plays twice as fast again.</summary>
    public float Speed
    {
        get { lock (_lock) return _stream.Speed; }
        set
        {
            lock (_lock) _stream.Speed = value;
            ScheduleRendering();
        }
    }

    /// <summary>Seconds of listening to keep rendered ahead of what is being heard. Default 30.</summary>
    public double Lookahead
    {
        get { lock (_lock) return _lookahead; }
        set
        {
            lock (_lock) _lookahead = Math.Max(1, value);
            ScheduleRendering();
        }
    }

    /// <summary>The index in the text of what is being heard now (approximate within a sentence).</summary>
    public int CharacterPosition { get { lock (_lock) return Position(); } }

    /// <summary>The sentence being heard now.</summary>
    public int SentenceIndex { get { lock (_lock) return SentenceAt(Position()); } }

    /// <summary>The last <see cref="Read"/> returned less than asked because the next sentence was being rendered.</summary>
    public bool IsBuffering { get { lock (_lock) return _buffering; } }

    /// <summary>Everything has been read.</summary>
    public bool IsFinished { get { lock (_lock) return _flushed && _stream.FramesAvailable == 0; } }

    /// <summary>The most recent rendering failure, if any. A sentence that fails is skipped.</summary>
    public Exception? LastError { get { lock (_lock) return _failure; } }

    /// <summary>
    /// Changes the speechwarp stream's other settings (nonlinear amount, heard pause, floor blend, rhythm ...)
    /// safely. Do not keep the stream.
    /// </summary>
    public void Configure(Action<SpeechwarpStream> change)
    {
        lock (_lock) change(_stream);
    }

    /// <summary>Fills <paramref name="samples"/> with what to play next; returns the frames written. Never waits.</summary>
    public int Read(Span<float> samples)
    {
        int frames;
        lock (_lock)
        {
            Feed(samples.Length);
            frames = _stream.Read(samples);
            _buffering = frames < samples.Length && !_flushed;
        }
        ScheduleRendering();
        return frames;
    }

    /// <summary>Continues from the start of the sentence containing character <paramref name="index"/>.</summary>
    public void Seek(int index)
    {
        lock (_lock)
        {
            _feeding = SentenceAt(index);
            _fed = 0;
            _stream.Reset();
            _segments.Clear();
            _written = 0;
            _flushed = false;
        }
        ScheduleRendering();
    }

    // Feeding the stream (with the lock held).

    private void Feed(int wanted)
    {
        while (_stream.FramesAvailable < wanted && !_flushed)
        {
            if (_feeding >= Sentences.Count)
            {
                _stream.Flush();
                _flushed = true;
                return;
            }
            if (_rendered.TryGetValue(_feeding, out var done) && done && !_cache.ContainsKey(_feeding))
            {
                _feeding++; // failed or empty: skip it
                _fed = 0;
                continue;
            }
            if (!_cache.TryGetValue(_feeding, out var audio))
                return; // not rendered yet
            var take = Math.Min(4096, audio.Length - _fed);
            if (take > 0)
            {
                _stream.Write(audio.AsSpan(_fed, take));
                if (_segments.Count > 0 && _segments[^1].Sentence == _feeding)
                    _segments[^1] = _segments[^1] with { Frames = _segments[^1].Frames + take };
                else
                    _segments.Add((_written, _feeding, take));
                _written += take;
                _fed += take;
            }
            if (_fed >= audio.Length)
            {
                _feeding++;
                _fed = 0;
                foreach (var old in _cache.Keys.Where(k => k < _feeding - 2).ToList())
                    _cache.Remove(old); // keep a little behind for short seeks
                foreach (var old in _rendered.Keys.Where(k => k < _feeding - 2).ToList())
                    _rendered.Remove(old);
            }
        }
    }

    private int Position()
    {
        var heard = _stream.Position;
        var index = _segments.FindLastIndex(s => s.InputStart <= heard);
        if (index < 0)
            return _feeding < Sentences.Count ? Sentences[_feeding].Start : Text.Length;
        var segment = _segments[index];
        var sentence = Sentences[segment.Sentence];
        var total = Math.Max(1, _cache.TryGetValue(segment.Sentence, out var audio) ? audio.Length : segment.Frames);
        var into = (double)(heard - segment.InputStart) / total;
        return sentence.Start + Math.Min(sentence.Length, (int)(into * sentence.Length));
    }

    private int SentenceAt(int index)
    {
        for (var i = Sentences.Count - 1; i >= 0; i--)
            if (Sentences[i].Start <= index)
                return i;
        return 0;
    }

    // Rendering ahead: the next sentence missing within the lookahead, one at a time.

    private void ScheduleRendering()
    {
        int index;
        string text;
        float rate;
        lock (_lock)
        {
            if (_renderingNow || _disposed)
                return;
            var seconds = 0.0;
            var speed = Math.Max(_stream.Speed, 0.05);
            index = -1;
            text = "";
            rate = _rate;
            for (var i = _feeding; i < Sentences.Count && seconds < _lookahead; i++)
            {
                if (_cache.TryGetValue(i, out var audio))
                {
                    seconds += (double)audio.Length / SampleRate / speed;
                }
                else if (!_rendered.ContainsKey(i))
                {
                    _rendered[i] = false;
                    _renderingNow = true;
                    index = i;
                    text = Sentences[i].Text;
                    break;
                }
            }
            if (index < 0)
                return;
        }
        _ = RenderOneAsync(index, text, rate);
    }

    private async Task RenderOneAsync(int index, string text, float rate)
    {
        try
        {
            var speech = await _renderer.RenderAsync(text, rate).ConfigureAwait(false);
            var samples = speech.SampleRate == SampleRate ? speech.Samples : Resample(speech.Samples, speech.SampleRate, SampleRate);
            lock (_lock)
            {
                if (_rate == rate && index >= _feeding - 2)
                {
                    if (samples.Length > 0)
                        _cache[index] = samples;
                    _rendered[index] = true;
                }
                else
                {
                    _rendered.Remove(index); // the rate changed meanwhile, or it is behind us
                }
            }
        }
        catch (Exception e)
        {
            lock (_lock)
            {
                _failure = e;
                _rendered[index] = true;
            }
        }
        lock (_lock) _renderingNow = false;
        ScheduleRendering();
    }

    /// <summary>
    /// Sentences by the system's tokenizer; any longer than 400 characters are cut at the last comma, semicolon or
    /// space before that, so that rendering never stalls on one enormous sentence.
    /// </summary>
    public static IReadOnlyList<Sentence> Split(string text)
    {
        var result = new List<Sentence>();
        using var tokenizer = new NLTokenizer(NLTokenUnit.Sentence) { String = text };
        foreach (var value in tokenizer.GetTokens(new NSRange(0, text.Length)))
        {
            var range = value.RangeValue;
            int start = (int)range.Location, end = (int)(range.Location + range.Length);
            while (end - start > MaxSentence)
            {
                var limit = start + MaxSentence;
                var cut = text.LastIndexOfAny([',', ';', ':'], limit - 1, MaxSentence);
                cut = cut > start ? cut + 1 : text.LastIndexOf(' ', limit - 1, MaxSentence) is var space && space > start ? space : limit;
                result.Add(new Sentence(start, cut - start, text[start..cut]));
                start = cut;
            }
            if (!string.IsNullOrWhiteSpace(text[start..end]))
                result.Add(new Sentence(start, end - start, text[start..end]));
        }
        return result;
    }

    private static float[] Resample(float[] samples, int source, int target)
    {
        if (source == target || samples.Length == 0)
            return samples;
        var result = new float[(int)((long)samples.Length * target / source)];
        for (var i = 0; i < result.Length; i++)
        {
            var x = (double)i * source / target;
            var j = (int)x;
            var f = (float)(x - j);
            result[i] = j + 1 < samples.Length ? samples[j] * (1 - f) + samples[j + 1] * f : samples[^1];
        }
        return result;
    }

    /// <summary>Stops rendering and releases the stream and the synthesiser.</summary>
    public void Dispose()
    {
        lock (_lock)
        {
            if (_disposed)
                return;
            _disposed = true;
            _stream.Dispose();
        }
        _renderer.Dispose();
    }
}
