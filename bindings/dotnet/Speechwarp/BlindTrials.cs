using System;

namespace Speechwarp;

/// <summary>
/// Designs a listener's own blind A/B comparisons: which setting to compare next at a speed, which two of its
/// values, in what order, and how the results add up in each speed band (whole numbers: 4 to 5, 5 to 6, ...).
/// Settings and their values are numbers here; the caller gives them meaning. Deterministic for a given seed.
/// </summary>
/// <remarks>Not thread safe. Use it from one thread, or lock around it.</remarks>
public sealed class BlindTrials : IDisposable
{
    private readonly TrialsHandle _handle;

    /// <summary>Creates an empty set of trials.</summary>
    /// <param name="seed">Seeds the random choices.</param>
    /// <exception cref="OutOfMemoryException">The native object could not be allocated.</exception>
    public BlindTrials(ulong seed = 0)
    {
        nint trials = Native.speechwarp_trials_create(seed);
        if (trials == 0)
            throw new OutOfMemoryException("speechwarp could not allocate the trials.");
        _handle = new TrialsHandle(trials);
    }

    /// <summary>Adds a setting.</summary>
    /// <returns>Its number (0, 1, ...), or -1.</returns>
    public int AddSetting() => Native.speechwarp_trials_add_setting(_handle);

    /// <summary>Adds a value to compare to a setting.</summary>
    /// <returns>Its number within the setting, or -1 (duplicate, bad setting).</returns>
    public int AddValue(int setting, double value) => Native.speechwarp_trials_add_value(_handle, setting, value);

    /// <summary>Leaves a setting out of <see cref="Next"/> while false (say, when its method is not available). True by default.</summary>
    public void SetAvailable(int setting, bool available) =>
        Native.speechwarp_trials_set_available(_handle, setting, available ? 1 : 0);

    /// <summary>Records a trial at <paramref name="speed"/>.</summary>
    /// <param name="setting">The setting compared.</param>
    /// <param name="speed">The speed of the trial.</param>
    /// <param name="firstValue">The value heard first; it must have been added.</param>
    /// <param name="secondValue">The value heard second; it must have been added.</param>
    /// <param name="firstScore">Score for the first, 0 to 1.</param>
    /// <param name="secondScore">Score for the second, 0 to 1.</param>
    /// <param name="preferred">-1 the first, 1 the second, 0 neither.</param>
    /// <returns>True if recorded, false if an argument was invalid.</returns>
    public bool Add(int setting, double speed, double firstValue, double secondValue, double firstScore,
        double secondScore, int preferred) =>
        Native.speechwarp_trials_add(_handle, setting, speed, firstValue, secondValue, firstScore, secondScore,
            preferred) != 0;

    /// <summary>
    /// Chooses the next trial at <paramref name="speed"/>: the available setting with the fewest trials in its band
    /// (ties at random), its pair of values compared least (ties at random), in random order.
    /// </summary>
    /// <returns>The setting and the two values in the order to play them, or null if no setting has two values.</returns>
    public (int Setting, double First, double Second)? Next(double speed)
    {
        int setting = Native.speechwarp_trials_next(_handle, speed);
        if (setting < 0)
            return null;
        return (setting, Native.speechwarp_trials_next_first(_handle), Native.speechwarp_trials_next_second(_handle));
    }

    /// <summary>Comparisons the value won, in the band of <paramref name="speed"/>.</summary>
    public int Won(int setting, double speed, int value) => Native.speechwarp_trials_won(_handle, setting, speed, value);

    /// <summary>Comparisons the value lost, in the band of <paramref name="speed"/>.</summary>
    public int Lost(int setting, double speed, int value) => Native.speechwarp_trials_lost(_handle, setting, speed, value);

    /// <summary>Comparisons the value tied, in the band of <paramref name="speed"/>.</summary>
    public int Tied(int setting, double speed, int value) => Native.speechwarp_trials_tied(_handle, setting, speed, value);

    /// <summary>Trials the value was heard in, in the band of <paramref name="speed"/>.</summary>
    public int Heard(int setting, double speed, int value) => Native.speechwarp_trials_heard(_handle, setting, speed, value);

    /// <summary>The value's mean score in the band of <paramref name="speed"/>, or null if it was never heard.</summary>
    public double? MeanScore(int setting, double speed, int value)
    {
        double mean = Native.speechwarp_trials_mean_score(_handle, setting, speed, value);
        return double.IsNaN(mean) ? null : mean;
    }

    /// <summary>
    /// The value with a reliable win in the band of <paramref name="speed"/>, or null. A value wins when it has been
    /// heard in at least 5 trials, has met every other value in at least 3, and against each the Bayes factor for
    /// "preferred" over "no preference" is at least 1 / (1 - confidence).
    /// </summary>
    public int? Winner(int setting, double speed)
    {
        int winner = Native.speechwarp_trials_winner(_handle, setting, speed);
        return winner < 0 ? null : winner;
    }

    /// <summary>Sets the confidence <see cref="Winner"/> needs; default 0.95.</summary>
    public void SetConfidence(double confidence) => Native.speechwarp_trials_set_confidence(_handle, confidence);

    /// <summary>Frees the native object.</summary>
    public void Dispose() => _handle.Dispose();
}
