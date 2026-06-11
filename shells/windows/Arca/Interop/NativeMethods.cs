// P/Invoke surface for the arca core C ABI (core/include/arca/*.h).
// Control plane only — the render data plane lives entirely in the core.

using System.Runtime.InteropServices;

namespace Arca.Interop;

internal enum ArcaStatus
{
    Ok = 0,
    InvalidArg = -1,
    Engine = -2,
    Graphics = -3,
    Unsupported = -4,
    NoMem = -5,
}

public enum ArcaPlayState
{
    Idle = 0,
    Loading = 1,
    Playing = 2,
    Paused = 3,
    Ended = 4,
}

internal enum ArcaEventKind
{
    StateChanged = 1,
    Position = 2,
    Duration = 3,
    FileLoaded = 4,
    PlaybackError = 5,
    Log = 6,
    Shutdown = 7,
}

[StructLayout(LayoutKind.Sequential)]
internal struct ArcaEvent
{
    public int Kind;
    public int State;
    public double Seconds;
    public IntPtr Message;  // valid only inside the callback
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ArcaEventCallback(IntPtr ev, IntPtr userdata);

[StructLayout(LayoutKind.Sequential)]
internal struct ArcaEngineParams
{
    public IntPtr OnEvent;
    public IntPtr Userdata;
    [MarshalAs(UnmanagedType.I1)] public bool VerboseLog;
}

[StructLayout(LayoutKind.Sequential)]
public struct ArcaRenderTargetInfo
{
    [MarshalAs(UnmanagedType.I1)] public bool HdrActive;
    public float DisplayMaxNits;
    public float DisplayMinNits;
    public int Width;
    public int Height;
}

internal static partial class NativeMethods
{
    private const string Dll = "arca_core";

    // --- engine -------------------------------------------------------------

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr arca_engine_create(ref ArcaEngineParams p);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void arca_engine_destroy(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_load(
        IntPtr engine, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_stop(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_set_paused(
        IntPtr engine, [MarshalAs(UnmanagedType.I1)] bool paused);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_toggle_paused(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_seek_absolute(IntPtr engine, double seconds);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_seek_relative(IntPtr engine, double delta);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaPlayState arca_engine_state(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern double arca_engine_position(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern double arca_engine_duration(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_set_volume(IntPtr engine, double volume);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern double arca_engine_volume(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_engine_set_muted(
        IntPtr engine, [MarshalAs(UnmanagedType.I1)] bool muted);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    internal static extern bool arca_engine_muted(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr arca_engine_get_property_string(
        IntPtr engine, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void arca_string_free(IntPtr s);

    // --- render session -------------------------------------------------------

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr arca_render_create_panel(
        IntPtr engine, IntPtr panelNative, IntPtr hwndForMonitor,
        int width, int height, float scaleX, float scaleY);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void arca_render_resize(IntPtr session, int width, int height);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void arca_render_set_scale(IntPtr session, float scaleX, float scaleY);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern ArcaStatus arca_render_get_target_info(
        IntPtr session, out ArcaRenderTargetInfo info);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void arca_render_destroy(IntPtr session);

    internal static string? GetPropertyString(IntPtr engine, string name)
    {
        IntPtr p = arca_engine_get_property_string(engine, name);
        if (p == IntPtr.Zero)
            return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { arca_string_free(p); }
    }
}
