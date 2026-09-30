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
    [ModuleInitializer]
    internal static void FindFrameworkOnIos()
    {
        if (OperatingSystem.IsIOS())
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, _, _) =>
                name == Library && NativeLibrary.TryLoad("@rpath/speechwarp.framework/speechwarp", out nint handle)
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
