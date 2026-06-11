using Arca.Interop;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Storage.Pickers;

namespace Arca;

public sealed partial class MainWindow : Window
{
    private static readonly string[] OpenExtensions =
        [".mkv", ".mp4", ".mov", ".m4v", ".webm", ".avi", ".ts", ".m2ts", ".hevc"];

    private const string GlyphPlay = "";
    private const string GlyphPause = "";
    private const string GlyphVolume = "";
    private const string GlyphMute = "";

    private PlayerEngine? _engine;
    private readonly string? _startupFile;
    private string _currentFile = "";
    private bool _updatingSliderFromEngine;
    private bool _scrubbing;

    public MainWindow(string? startupFile)
    {
        _startupFile = startupFile;
        InitializeComponent();
        Title = "Arca";
        VideoPanel.Loaded += OnPanelLoaded;
        Closed += OnClosed;

        // Track thumb drags even though Slider handles pointer events itself,
        // so engine position updates don't fight the user's scrub.
        SeekSlider.AddHandler(UIElement.PointerPressedEvent,
            new PointerEventHandler((_, _) => _scrubbing = true), true);
        SeekSlider.AddHandler(UIElement.PointerReleasedEvent,
            new PointerEventHandler((_, _) => _scrubbing = false), true);
        SeekSlider.AddHandler(UIElement.PointerCaptureLostEvent,
            new PointerEventHandler((_, _) => _scrubbing = false), true);
    }

    private void OnPanelLoaded(object sender, RoutedEventArgs e)
    {
        if (_engine is not null)
            return;
        _engine = new PlayerEngine(DispatcherQueue);
        if (!_engine.AttachToPanel(VideoPanel, this))
        {
            IdleHint.Text = "Render initialization failed — see debug log.";
            return;
        }

        _engine.StateChanged += OnEngineState;
        _engine.PositionChanged += OnEnginePosition;
        _engine.DurationChanged += OnEngineDuration;
        _engine.FileLoaded += OnEngineFileLoaded;
        _engine.PlaybackError += msg => IdleHint.Text = $"Playback error: {msg}";

        if (_startupFile is not null)
            LoadFile(_startupFile);
    }

    private void LoadFile(string path)
    {
        if (_engine is null)
            return;
        _currentFile = Path.GetFileName(path);
        IdleHint.Visibility = Visibility.Collapsed;
        _engine.Load(path);
    }

    // --- engine events (UI thread) -------------------------------------------

    private void OnEngineState(ArcaPlayState state)
    {
        PlayPauseButton.IsEnabled = state is not ArcaPlayState.Idle;
        SeekSlider.IsEnabled = state is ArcaPlayState.Playing or ArcaPlayState.Paused
                                        or ArcaPlayState.Ended;
        // Play glyph E768, Pause glyph E769.
        PlayPauseIcon.Glyph = state == ArcaPlayState.Playing ? GlyphPause : GlyphPlay;

        string status = state switch
        {
            ArcaPlayState.Idle => "Arca",
            ArcaPlayState.Loading => $"Arca — loading {_currentFile}",
            ArcaPlayState.Playing => $"Arca — {_currentFile}",
            ArcaPlayState.Paused => $"Arca — {_currentFile} (paused)",
            ArcaPlayState.Ended => $"Arca — {_currentFile} (ended)",
            _ => "Arca",
        };
        var info = _engine?.TargetInfo;
        if (info is { HdrActive: true } i && state == ArcaPlayState.Playing)
            status += $"  [HDR {i.DisplayMaxNits:F0} nits]";
        Title = status;
    }

    private void OnEnginePosition(double seconds)
    {
        if (seconds < 0)
            return;
        PositionText.Text = FormatTime(seconds);
        if (!_scrubbing)
        {
            _updatingSliderFromEngine = true;
            SeekSlider.Value = seconds;
            _updatingSliderFromEngine = false;
        }
    }

    private void OnEngineDuration(double seconds)
    {
        if (seconds <= 0)
            return;
        DurationText.Text = FormatTime(seconds);
        _updatingSliderFromEngine = true;
        SeekSlider.Maximum = seconds;
        _updatingSliderFromEngine = false;
    }

    private void OnEngineFileLoaded()
    {
        // Surface what the engine actually negotiated (diagnostics parity
        // with hdr-verify; visible in the debug log).
        System.Diagnostics.Debug.WriteLine(
            $"[arca] video-out-params: {_engine?.GetDiagnosticProperty("video-out-params")}");
    }

    private static string FormatTime(double seconds)
    {
        var t = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss") : t.ToString(@"m\:ss");
    }

    // --- transport interactions ------------------------------------------------

    private void OnPlayPause(object sender, RoutedEventArgs e) => _engine?.TogglePause();

    private void OnSeekSliderChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_updatingSliderFromEngine || _engine is null)
            return;
        // Live scrub: the engine coalesces flooding seeks (keyframe-fast),
        // so every user-driven value change can seek directly.
        _engine.SeekAbsolute(e.NewValue);
    }

    private void OnToggleMute(object sender, RoutedEventArgs e) => ToggleMute();

    private void ToggleMute()
    {
        if (_engine is null)
            return;
        _engine.Muted = !_engine.Muted;
        MuteIcon.Glyph = _engine.Muted ? GlyphMute : GlyphVolume;
    }

    private async void OnOpenFile(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker();
        foreach (var ext in OpenExtensions)
            picker.FileTypeFilter.Add(ext);
        WinRT.Interop.InitializeWithWindow.Initialize(
            picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
        var file = await picker.PickSingleFileAsync();
        if (file is not null)
            LoadFile(file.Path);
    }

    private void OnExit(object sender, RoutedEventArgs e) => Close();

    // --- keyboard accelerators ---------------------------------------------------

    private void OnAccelTogglePause(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { _engine?.TogglePause(); a.Handled = true; }

    private void OnAccelSeekBack(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { _engine?.SeekRelative(-5); a.Handled = true; }

    private void OnAccelSeekForward(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { _engine?.SeekRelative(5); a.Handled = true; }

    private void OnAccelSeekBackLong(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { _engine?.SeekRelative(-60); a.Handled = true; }

    private void OnAccelSeekForwardLong(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { _engine?.SeekRelative(60); a.Handled = true; }

    private void OnAccelVolumeUp(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (_engine is not null) _engine.Volume += 5; a.Handled = true; }

    private void OnAccelVolumeDown(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (_engine is not null) _engine.Volume -= 5; a.Handled = true; }

    private void OnAccelToggleMute(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { ToggleMute(); a.Handled = true; }

    private void OnAccelToggleFullscreen(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    {
        bool isFull = AppWindow.Presenter.Kind == AppWindowPresenterKind.FullScreen;
        AppWindow.SetPresenter(isFull ? AppWindowPresenterKind.Default
                                      : AppWindowPresenterKind.FullScreen);
        AppMenuBar.Visibility = isFull ? Visibility.Visible : Visibility.Collapsed;
        TransportBar.Visibility = isFull ? Visibility.Visible : Visibility.Collapsed;
        a.Handled = true;
    }

    // --- panel/surface plumbing ----------------------------------------------------

    private void OnPanelSizeChanged(object sender, SizeChangedEventArgs e) =>
        _engine?.ResizePanel(e.NewSize.Width, e.NewSize.Height,
                             VideoPanel.CompositionScaleX, VideoPanel.CompositionScaleY);

    private void OnPanelScaleChanged(Microsoft.UI.Xaml.Controls.SwapChainPanel sender, object args) =>
        _engine?.ResizePanel(sender.ActualWidth, sender.ActualHeight,
                             sender.CompositionScaleX, sender.CompositionScaleY);

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _engine?.Dispose();
        _engine = null;
    }
}
