// Managed wrapper over the arca engine: owns the native handles, keeps the
// event callback alive, and marshals engine-thread events onto the UI
// DispatcherQueue. The shell never touches frames — the render session is
// created onto the SwapChainPanel and driven entirely inside the core.

using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Arca.Interop;

public sealed class PlayerEngine : IDisposable
{
    private readonly DispatcherQueue _dispatcher;
    private readonly ArcaEventCallback _callback;  // keeps the delegate alive
    private IntPtr _engine;
    private IntPtr _session;

    public event Action<ArcaPlayState>? StateChanged;
    public event Action<double>? PositionChanged;
    public event Action<double>? DurationChanged;
    public event Action? FileLoaded;
    public event Action<string>? PlaybackError;

    public PlayerEngine(DispatcherQueue dispatcher)
    {
        _dispatcher = dispatcher;
        _callback = OnNativeEvent;
        var p = new ArcaEngineParams
        {
            OnEvent = Marshal.GetFunctionPointerForDelegate(_callback),
            Userdata = IntPtr.Zero,
            VerboseLog = false,
        };
        _engine = NativeMethods.arca_engine_create(ref p);
        if (_engine == IntPtr.Zero)
            throw new InvalidOperationException("arca_engine_create failed");
    }

    public bool AttachToPanel(SwapChainPanel panel, Window window)
    {
        if (_session != IntPtr.Zero)
            return true;

        IntPtr hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window);
        // Borrowed IUnknown of the panel's WinRT object; the core QIs
        // ISwapChainPanelNative and AddRefs what it keeps.
        IntPtr panelUnknown = ((WinRT.IWinRTObject)panel).NativeObject.ThisPtr;

        float sx = panel.CompositionScaleX, sy = panel.CompositionScaleY;
        int w = Math.Max(1, (int)(panel.ActualWidth * sx));
        int h = Math.Max(1, (int)(panel.ActualHeight * sy));

        _session = NativeMethods.arca_render_create_panel(
            _engine, panelUnknown, hwnd, w, h, sx, sy);
        return _session != IntPtr.Zero;
    }

    public void ResizePanel(double width, double height, float scaleX, float scaleY)
    {
        if (_session == IntPtr.Zero)
            return;
        NativeMethods.arca_render_set_scale(_session, scaleX, scaleY);
        NativeMethods.arca_render_resize(
            _session,
            Math.Max(1, (int)(width * scaleX)),
            Math.Max(1, (int)(height * scaleY)));
    }

    public ArcaRenderTargetInfo? TargetInfo
    {
        get
        {
            if (_session == IntPtr.Zero)
                return null;
            return NativeMethods.arca_render_get_target_info(_session, out var info)
                       == ArcaStatus.Ok ? info : null;
        }
    }

    public void Load(string path) => NativeMethods.arca_engine_load(_engine, path);
    public void Stop() => NativeMethods.arca_engine_stop(_engine);
    public void TogglePause() => NativeMethods.arca_engine_toggle_paused(_engine);
    public void SetPaused(bool paused) => NativeMethods.arca_engine_set_paused(_engine, paused);
    public void SeekAbsolute(double seconds) => NativeMethods.arca_engine_seek_absolute(_engine, seconds);
    public void SeekRelative(double delta) => NativeMethods.arca_engine_seek_relative(_engine, delta);

    public ArcaPlayState State => NativeMethods.arca_engine_state(_engine);
    public double Position => NativeMethods.arca_engine_position(_engine);
    public double Duration => NativeMethods.arca_engine_duration(_engine);

    public double Volume
    {
        get => NativeMethods.arca_engine_volume(_engine);
        set => NativeMethods.arca_engine_set_volume(_engine, Math.Clamp(value, 0, 100));
    }

    public bool Muted
    {
        get => NativeMethods.arca_engine_muted(_engine);
        set => NativeMethods.arca_engine_set_muted(_engine, value);
    }

    public string? GetDiagnosticProperty(string name) =>
        _engine == IntPtr.Zero ? null : NativeMethods.GetPropertyString(_engine, name);

    private void OnNativeEvent(IntPtr evPtr, IntPtr _)
    {
        // Engine thread: copy everything out before returning.
        var ev = Marshal.PtrToStructure<ArcaEvent>(evPtr);
        var kind = (ArcaEventKind)ev.Kind;
        string? message = kind is ArcaEventKind.PlaybackError or ArcaEventKind.Log
            ? Marshal.PtrToStringUTF8(ev.Message) : null;

        if (kind == ArcaEventKind.Log)
        {
            Debug.WriteLine($"[arca] {message}");
            return;
        }

        _dispatcher.TryEnqueue(() =>
        {
            switch (kind)
            {
                case ArcaEventKind.StateChanged:
                    StateChanged?.Invoke((ArcaPlayState)ev.State);
                    break;
                case ArcaEventKind.Position:
                    PositionChanged?.Invoke(ev.Seconds);
                    break;
                case ArcaEventKind.Duration:
                    DurationChanged?.Invoke(ev.Seconds);
                    break;
                case ArcaEventKind.FileLoaded:
                    FileLoaded?.Invoke();
                    break;
                case ArcaEventKind.PlaybackError:
                    PlaybackError?.Invoke(message ?? "unknown engine error");
                    break;
            }
        });
    }

    public void Dispose()
    {
        if (_session != IntPtr.Zero)
        {
            NativeMethods.arca_render_destroy(_session);
            _session = IntPtr.Zero;
        }
        if (_engine != IntPtr.Zero)
        {
            NativeMethods.arca_engine_destroy(_engine);
            _engine = IntPtr.Zero;
        }
    }
}
