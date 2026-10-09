using System;
using System.Collections.Generic;
using System.Linq;
using AVFoundation;

namespace Speechwarp.Voice;

/// <summary>
/// A voice the system can speak with: one of Apple's Eloquence voices, or any other installed voice.
/// </summary>
/// <remarks>
/// Eloquence is the compact synthesiser many fast screen-reader listeners choose, because it stays crisp at high
/// rates. Apple includes it from iOS 16 (Eddy, Flo, Grandma, Grandpa, Reed, Rocko, Sandy and Shelley, in several
/// languages). The voices belong to the system: nothing is bundled.
/// </remarks>
/// <param name="Identifier">The system's identifier, such as "com.apple.eloquence.en-US.Reed".</param>
/// <param name="Name">The voice's name, such as "Reed".</param>
/// <param name="Language">A BCP 47 language tag, such as "en-US".</param>
public sealed record SpeechVoice(string Identifier, string Name, string Language)
{
    /// <summary>Whether this is one of the Eloquence voices.</summary>
    public bool IsEloquence => Identifier.Contains(".eloquence.", StringComparison.Ordinal);

    /// <summary>Every voice installed on this device.</summary>
    public static IReadOnlyList<SpeechVoice> All =>
        AVSpeechSynthesisVoice.GetSpeechVoices().Select(From).ToList();

    /// <summary>The Eloquence voices installed, optionally only those for a language ("en", "en-GB").</summary>
    public static IReadOnlyList<SpeechVoice> Eloquence(string? language = null) =>
        All.Where(v => v.IsEloquence && (language is null || v.Language.StartsWith(language, StringComparison.Ordinal)))
            .ToList();

    /// <summary>The voice with this identifier, or null if it is not installed.</summary>
    public static SpeechVoice? Find(string identifier) =>
        AVSpeechSynthesisVoice.FromIdentifier(identifier) is { } voice ? From(voice) : null;

    internal static SpeechVoice From(AVSpeechSynthesisVoice voice) => new(voice.Identifier, voice.Name, voice.Language);
}

/// <summary>
/// The rates the system accepts: 0.5 is ordinary speech and the maximum, 1, is several times faster (about four
/// times for Eloquence). Above that, <see cref="SpokenText.Speed"/> speeds the audio up further.
/// </summary>
public static class SpeechRate
{
    /// <summary>The slowest rate.</summary>
    public static float Minimum => AVSpeechUtterance.MinimumSpeechRate;
    /// <summary>Ordinary speech.</summary>
    public static float Normal => AVSpeechUtterance.DefaultSpeechRate;
    /// <summary>The fastest rate.</summary>
    public static float Maximum => AVSpeechUtterance.MaximumSpeechRate;
}
