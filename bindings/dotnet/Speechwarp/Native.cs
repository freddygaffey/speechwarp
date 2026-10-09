using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Speechwarp;

/// <summary>The functions of include/speechwarp.h.</summary>
internal static unsafe partial class Native
{
    private const string Library = "speechwarp";

    // On iOS the library is a framework inside the app, which the runtime does not find by its bare name.
    // This runs when the assembly loads and not in a static constructor, because Mono looks a function up
    // when it compiles the caller, which is before the constructor of this class would run. (A simulator test
    // app threw DllNotFoundException both without this and with it in a static constructor.)
#pragma warning disable CA2255 // Module initializers are discouraged in libraries; this is what they are for.
    //
    // On Windows the native library is libspeechwarp.dll: file names there ignore case, so speechwarp.dll would be
    // the same file as this assembly, Speechwarp.dll, wherever the two land in one folder (the tests' output, or
    // an app published for one runtime). Plain "speechwarp" would then find this assembly and no functions in it.
    [ModuleInitializer]
    internal static void FindNativeLibrary()
    {
        if (OperatingSystem.IsIOS())
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, _, _) =>
                name == Library && NativeLibrary.TryLoad("@rpath/speechwarp.framework/speechwarp", out nint handle)
                    ? handle
                    : 0);
        }
        else if (OperatingSystem.IsWindows())
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, assembly, searchPath) =>
                name == Library && NativeLibrary.TryLoad("libspeechwarp.dll", assembly, searchPath, out nint handle)
                    ? handle
                    : 0);
        }
    }
#pragma warning restore CA2255

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_version();

    [LibraryImport(Library)]
    internal static partial nint speechwarp_create(int sampleRate, int channels);

    [LibraryImport(Library)]
    internal static partial void speechwarp_destroy(nint stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_speed(StreamHandle stream, float speed);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_speed(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_nonlinear(StreamHandle stream, float amount);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_nonlinear(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_pause_cap(StreamHandle stream, float value);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_pause_cap(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_speed_floor(StreamHandle stream, float value);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_speed_floor(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_rhythm_gap(StreamHandle stream, float value);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_rhythm_gap(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_rhythm_rate(StreamHandle stream, float value);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_rhythm_rate(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_keep_speed(StreamHandle stream, int enabled);

    [LibraryImport(Library)]
    internal static partial int speechwarp_get_keep_speed(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial double speechwarp_syllable_rate(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial int speechwarp_write(StreamHandle stream, float* samples, int frames);

    [LibraryImport(Library)]
    internal static partial int speechwarp_write_i16(StreamHandle stream, short* samples, int frames);

    [LibraryImport(Library)]
    internal static partial int speechwarp_read(StreamHandle stream, float* samples, int maxFrames);

    [LibraryImport(Library)]
    internal static partial int speechwarp_read_i16(StreamHandle stream, short* samples, int maxFrames);

    [LibraryImport(Library)]
    internal static partial int speechwarp_available(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial int speechwarp_flush(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_reset(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial long speechwarp_position(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_heard_pause(StreamHandle stream, float seconds, float fromSpeed);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_heard_pause(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_heard_pause_from(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial void speechwarp_set_floor_blend(StreamHandle stream, float fraction, float fromSpeed, float fullSpeed);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_floor_blend(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_floor_blend_from(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial float speechwarp_get_floor_blend_full(StreamHandle stream);

    [LibraryImport(Library)]
    internal static partial nint speechwarp_syllables_create(int sampleRate, int channels);

    [LibraryImport(Library)]
    internal static partial void speechwarp_syllables_destroy(nint counter);

    [LibraryImport(Library)]
    internal static partial int speechwarp_syllables_write(SyllablesHandle counter, float* samples, int frames);

    [LibraryImport(Library)]
    internal static partial int speechwarp_syllables_write_i16(SyllablesHandle counter, short* samples, int frames);

    [LibraryImport(Library)]
    internal static partial double speechwarp_syllables_rate(SyllablesHandle counter, double windowSeconds, double minimumSeconds);

    [LibraryImport(Library)]
    internal static partial void speechwarp_syllables_reset(SyllablesHandle counter);

    [LibraryImport(Library)]
    internal static partial nint speechwarp_trainer_create(ulong seed);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trainer_destroy(nint trainer);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trainer_set_weight(TrainerHandle trainer, int kind, double weight);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_get_weight(TrainerHandle trainer, int kind);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trainer_set_param(TrainerHandle trainer, int param, double value);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_get_param(TrainerHandle trainer, int param);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_add_measure(TrainerHandle trainer, int kind, double score, double items, double rate, double time);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trainer_test_begin(TrainerHandle trainer, double priorRate, double time);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_test_rate(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_test_done(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_test_end(TrainerHandle trainer, double time);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_threshold(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_threshold_low(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_threshold_high(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trainer_session_begin(TrainerHandle trainer, int plan, double time);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_session_rate(TrainerHandle trainer, double time);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_session_end(TrainerHandle trainer, double listeningHours, double time);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_add_retention(TrainerHandle trainer, int session, double score, double items, double delaySeconds, double time);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_next_plan(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_plan_effect(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_plan_effect_sd(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_plan_retention(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_plan_retention_sd(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trainer_plan_sessions(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_plan_best_probability(TrainerHandle trainer, int plan);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_trend(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trainer_trend_sd(TrainerHandle trainer);

    [LibraryImport(Library)]
    internal static partial nint speechwarp_trials_create(ulong seed);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trials_destroy(nint trials);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_add_setting(TrialsHandle trials);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_add_value(TrialsHandle trials, int setting, double value);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trials_set_available(TrialsHandle trials, int setting, int available);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_add(TrialsHandle trials, int setting, double speed, double firstValue, double secondValue, double firstScore, double secondScore, int preferred);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_next(TrialsHandle trials, double speed);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trials_next_first(TrialsHandle trials);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trials_next_second(TrialsHandle trials);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_won(TrialsHandle trials, int setting, double speed, int value);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_lost(TrialsHandle trials, int setting, double speed, int value);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_tied(TrialsHandle trials, int setting, double speed, int value);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_heard(TrialsHandle trials, int setting, double speed, int value);

    [LibraryImport(Library)]
    internal static partial double speechwarp_trials_mean_score(TrialsHandle trials, int setting, double speed, int value);

    [LibraryImport(Library)]
    internal static partial int speechwarp_trials_winner(TrialsHandle trials, int setting, double speed);

    [LibraryImport(Library)]
    internal static partial void speechwarp_trials_set_confidence(TrialsHandle trials, double confidence);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial double speechwarp_score_words(string reference, string heard, int* counts);
}

/// <summary>Owns a native stream, so it is freed even if the wrapper is never disposed.</summary>
internal sealed class StreamHandle : SafeHandle
{
    public StreamHandle(nint stream) : base(0, ownsHandle: true) => SetHandle(stream);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_destroy(handle);
        return true;
    }
}

/// <summary>Owns a native syllable counter.</summary>
internal sealed class SyllablesHandle : SafeHandle
{
    public SyllablesHandle(nint counter) : base(0, ownsHandle: true) => SetHandle(counter);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_syllables_destroy(handle);
        return true;
    }
}

/// <summary>Owns a native listener trainer.</summary>
internal sealed class TrainerHandle : SafeHandle
{
    public TrainerHandle(nint trainer) : base(0, ownsHandle: true) => SetHandle(trainer);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_trainer_destroy(handle);
        return true;
    }
}

/// <summary>Owns a native set of blind trials.</summary>
internal sealed class TrialsHandle : SafeHandle
{
    public TrialsHandle(nint trials) : base(0, ownsHandle: true) => SetHandle(trials);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_trials_destroy(handle);
        return true;
    }
}
