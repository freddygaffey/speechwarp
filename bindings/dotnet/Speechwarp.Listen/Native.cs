using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Speechwarp.Listen;

/// <summary>The functions of listen/include/speechwarp_listen.h.</summary>
internal static unsafe partial class Native
{
    private const string Library = "speechwarp_listen";

    internal const int Ok = 0;
    internal const int ErrorArgument = -1;
    internal const int ErrorMemory = -2;
    internal const int ErrorEngine = -3;
    internal const int ErrorCancelled = -4;
    internal const int ErrorFinished = -5;

    internal const int LoadGpu = 1;

    // As in the Speechwarp package (see its Native.cs): on iOS the library is a framework inside the app, which the
    // runtime does not find by its bare name, and this has to run when the assembly loads, before Mono compiles a
    // caller. On Windows the library is libspeechwarp_listen.dll, so that it can never be taken for this assembly,
    // Speechwarp.Listen.dll, in a file system that ignores case.
#pragma warning disable CA2255 // Module initializers are discouraged in libraries; this is what they are for.
    [ModuleInitializer]
    internal static void FindNativeLibrary()
    {
        if (OperatingSystem.IsIOS())
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, _, _) =>
                name == Library && NativeLibrary.TryLoad("@rpath/speechwarp_listen.framework/speechwarp_listen",
                    out nint handle)
                    ? handle
                    : 0);
        }
        else if (OperatingSystem.IsWindows())
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, assembly, searchPath) =>
                name == Library && NativeLibrary.TryLoad("libspeechwarp_listen.dll", assembly, searchPath, out nint handle)
                    ? handle
                    : 0);
        }
    }
#pragma warning restore CA2255

    /// <summary>A UTF-8 string owned by the library, or null.</summary>
    internal static string? Text(byte* text) => text == null ? null : Marshal.PtrToStringUTF8((nint)text);

    // ---- About the library

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_version();

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_engine_version();

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_system_info();

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_error_message(int code);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_set_log(int on);

    // ---- Catalogue

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_catalogue_count();

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int speechwarp_listen_catalogue_find(string id);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_id(int index);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_name(int index);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_languages(int index);

    [LibraryImport(Library)]
    internal static partial long speechwarp_listen_catalogue_size(int index);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_file_name(int index);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_url(int index);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_catalogue_sha256(int index);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_catalogue_relative_speed(int index);

    // ---- Models

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial nint speechwarp_listen_model_load(string path, int threads, int flags);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_model_free(nint model);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_model_multilingual(ModelHandle model);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_model_type(ModelHandle model);

    // ---- Options

    [LibraryImport(Library)]
    internal static partial nint speechwarp_listen_options_create();

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_options_free(nint options);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial int speechwarp_listen_options_set_language(OptionsHandle options, string? language);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_options_set_word_timestamps(OptionsHandle options, int on);

    [LibraryImport(Library, StringMarshalling = StringMarshalling.Utf8)]
    internal static partial void speechwarp_listen_options_set_prompt(OptionsHandle options, string? prompt);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_options_set_preset(OptionsHandle options, int preset);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_options_set_threads(OptionsHandle options, int threads);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_options_cancel(OptionsHandle options);

    // ---- A passage

    [LibraryImport(Library)]
    internal static partial nint speechwarp_listen_transcribe(ModelHandle model, float* samples, long count,
        int sampleRate, OptionsHandle options, int* error);

    // ---- Results

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_result_free(nint result);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_result_segment_count(ResultHandle result);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_result_segment_text(ResultHandle result, int segment);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_result_segment_start(ResultHandle result, int segment);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_result_segment_end(ResultHandle result, int segment);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_result_word_count(ResultHandle result, int segment);

    [LibraryImport(Library)]
    internal static partial byte* speechwarp_listen_result_word_text(ResultHandle result, int segment, int word);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_result_word_start(ResultHandle result, int segment, int word);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_result_word_end(ResultHandle result, int segment, int word);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_result_word_probability(ResultHandle result, int segment, int word);

    // ---- Sessions

    [LibraryImport(Library)]
    internal static partial nint speechwarp_listen_session_create(ModelHandle model, int sampleRate, OptionsHandle options);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_session_free(nint session);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_session_write(SessionHandle session, float* samples, long count);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_session_process(SessionHandle session);

    [LibraryImport(Library)]
    internal static partial nint speechwarp_listen_session_partial(SessionHandle session, int* error);

    [LibraryImport(Library)]
    internal static partial int speechwarp_listen_session_finish(SessionHandle session);

    [LibraryImport(Library)]
    internal static partial nint speechwarp_listen_session_take(SessionHandle session);

    [LibraryImport(Library)]
    internal static partial void speechwarp_listen_session_cancel(SessionHandle session);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_session_seconds_written(SessionHandle session);

    [LibraryImport(Library)]
    internal static partial double speechwarp_listen_session_seconds_recognised(SessionHandle session);

    /// <summary>The library's sentence for an error code.</summary>
    internal static string Message(int code) => Text(speechwarp_listen_error_message(code)) ?? $"error {code}";

    /// <summary>The exception for an error code, other than a cancel, which callers handle themselves.</summary>
    internal static Exception Failure(int code) => code switch
    {
        ErrorArgument => new ArgumentException(Message(code)),
        ErrorMemory => new OutOfMemoryException(Message(code)),
        ErrorFinished => new InvalidOperationException(Message(code)),
        _ => new InvalidOperationException(Message(code)),
    };
}

/// <summary>Owns a loaded model.</summary>
internal sealed class ModelHandle : SafeHandle
{
    public ModelHandle(nint model) : base(0, ownsHandle: true) => SetHandle(model);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_listen_model_free(handle);
        return true;
    }
}

/// <summary>Owns native options.</summary>
internal sealed class OptionsHandle : SafeHandle
{
    public OptionsHandle(nint options) : base(0, ownsHandle: true) => SetHandle(options);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_listen_options_free(handle);
        return true;
    }
}

/// <summary>Owns a result.</summary>
internal sealed class ResultHandle : SafeHandle
{
    public ResultHandle(nint result) : base(0, ownsHandle: true) => SetHandle(result);

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_listen_result_free(handle);
        return true;
    }
}

/// <summary>Owns a session, and keeps its model alive until the session is freed, as the library requires.</summary>
internal sealed class SessionHandle : SafeHandle
{
    private readonly ModelHandle _model;

    public SessionHandle(nint session, ModelHandle model) : base(0, ownsHandle: true)
    {
        bool added = false;
        model.DangerousAddRef(ref added);
        _model = model;
        SetHandle(session);
    }

    public override bool IsInvalid => handle == 0;

    protected override bool ReleaseHandle()
    {
        Native.speechwarp_listen_session_free(handle);
        _model.DangerousRelease();
        return true;
    }
}
