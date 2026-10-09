using System;
using System.Collections.Generic;
using System.Linq;
using Speechwarp.Transcription;

namespace Speechwarp.Voice;

// The parts of Apple speech recognition that are plain logic, kept apart (and free of iOS types) so that they can
// be tested without a recogniser: grouping timed words into segments, and choosing where to cut a long stream.

/// <summary>Groups timed words into segments.</summary>
internal static class TranscriptAssembly
{
    /// <summary>
    /// Groups words into segments, ending one after a sentence's closing punctuation, at a pause of
    /// <paramref name="pauseGap"/> seconds or more, or when it would grow longer than <paramref name="maxSeconds"/>.
    /// Words are moved later by <paramref name="offset"/> seconds. Segments keep their words only when
    /// <paramref name="keepWords"/> is true.
    /// </summary>
    public static List<TranscriptSegment> Segments(IEnumerable<TranscriptWord> words, double offset = 0,
        double pauseGap = 0.7, double maxSeconds = 30, bool keepWords = true)
    {
        var segments = new List<TranscriptSegment>();
        var current = new List<TranscriptWord>();

        void Close()
        {
            if (current.Count == 0)
                return;
            var text = string.Join(" ", current.Select(w => w.Text));
            segments.Add(new TranscriptSegment(text, current[0].Start, current[^1].End,
                keepWords ? current.ToArray() : []));
            current.Clear();
        }

        foreach (var word in words)
        {
            var text = word.Text.Trim();
            if (text.Length == 0)
                continue;
            var shifted = word with { Text = text, Start = word.Start + offset, End = word.End + offset };
            if (current.Count > 0 &&
                (shifted.Start - current[^1].End >= pauseGap || shifted.End - current[0].Start > maxSeconds))
                Close();
            current.Add(shifted);
            if (EndsSentence(text))
                Close();
        }
        Close();
        return segments;
    }

    /// <summary>Whether a word ends a sentence: a full stop, question or exclamation mark, possibly in quotes.</summary>
    public static bool EndsSentence(string word)
    {
        var trimmed = word.TrimEnd('"', '\'', '”', '’', ')', ']', '»');
        return trimmed.Length > 0 && ".?!…。？！".Contains(trimmed[^1]);
    }
}

/// <summary>
/// Splits a stream of audio into requests of a bounded length, cutting in a pause where it can.
/// </summary>
/// <remarks>
/// Apple's SFSpeechRecognizer is made for utterances, not hours, so a long input is given to it as a series of
/// requests. Audio passes through in blocks of 100 ms; once a request is <c>minSeconds</c> long, the first quiet
/// block ends it, cut in the middle of the block; at <c>maxSeconds</c> it ends wherever it is.
/// </remarks>
internal sealed class ChunkCutter
{
    /// <summary>Samples for the current request, or (when null) the end of it: what follows starts a new one.</summary>
    public readonly record struct Piece(float[]? Audio)
    {
        /// <summary>End the current request.</summary>
        public static Piece Cut => new(null);
        /// <summary>Whether this ends the current request.</summary>
        public bool IsCut => Audio is null;
    }

    private readonly int _blockFrames;
    private readonly int _minFrames;
    private readonly int _maxFrames;
    private readonly List<float> _block = [];
    private int _requestFrames;
    private float _peak;

    public ChunkCutter(int sampleRate, double minSeconds = 20, double maxSeconds = 50)
    {
        _blockFrames = Math.Max(1, sampleRate / 10);
        _minFrames = (int)(sampleRate * minSeconds);
        _maxFrames = Math.Max((int)(sampleRate * maxSeconds), _minFrames + _blockFrames);
    }

    /// <summary>Takes samples and returns what can be passed on now. Up to one block is held back.</summary>
    public List<Piece> Push(ReadOnlySpan<float> samples)
    {
        var pieces = new List<Piece>();
        var index = 0;
        while (index < samples.Length)
        {
            var take = Math.Min(_blockFrames - _block.Count, samples.Length - index);
            _block.AddRange(samples.Slice(index, take));
            index += take;
            if (_block.Count == _blockFrames)
            {
                Emit(_block.ToArray(), pieces);
                _block.Clear();
            }
        }
        return pieces;
    }

    /// <summary>Passes on whatever is held back, at the end of the input.</summary>
    public List<Piece> Flush()
    {
        var pieces = new List<Piece>();
        if (_block.Count == 0)
            return pieces;
        if (_requestFrames + _block.Count > _maxFrames)
        {
            pieces.Add(Piece.Cut);
            _requestFrames = 0;
        }
        pieces.Add(new Piece(_block.ToArray()));
        _requestFrames += _block.Count;
        _block.Clear();
        return pieces;
    }

    private void Emit(float[] block, List<Piece> pieces)
    {
        var sum = 0f;
        foreach (var sample in block)
            sum += sample * sample;
        var rms = MathF.Sqrt(sum / block.Length);
        // The loudness to compare with falls by about a factor of e every 20 s, so a loud start does not make
        // everything after it count as quiet.
        _peak = Math.Max(rms, _peak * 0.995f);
        var quiet = rms < 0.001f || rms < _peak * 0.03f;

        if (_requestFrames >= _minFrames && quiet)
        {
            var half = block.Length / 2;
            pieces.Add(new Piece(block[..half]));
            pieces.Add(Piece.Cut);
            pieces.Add(new Piece(block[half..]));
            _requestFrames = block.Length - half;
        }
        else if (_requestFrames + block.Length > _maxFrames)
        {
            pieces.Add(Piece.Cut);
            pieces.Add(new Piece(block));
            _requestFrames = block.Length;
        }
        else
        {
            pieces.Add(new Piece(block));
            _requestFrames += block.Length;
        }
    }
}
