using Arca.Interop;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Storage.Pickers;

namespace Arca;

public sealed partial class MainWindow : Window
{
    private static readonly string[] OpenExtensions =
        [".mkv", ".mp4", ".mov", ".m4v", ".webm", ".avi", ".ts", ".m2ts", ".hevc"];

    private enum AppSection { Home, Library, Search, Player, Queue, Settings }

    private sealed record OnlineGroup(string Key, IReadOnlyList<MediaInfo> Items)
    {
        public string CountLabel => $"{Items.Count} item{(Items.Count == 1 ? "" : "s")}";
        public MediaInfo? Primary => Items.FirstOrDefault();
    }

    private PlayerEngine? _engine;
    private LibraryStore? _store;
    private PlaybackQueue? _queue;
    private readonly string? _startupFile;
    private readonly DispatcherTimer _progressTimer = new() { Interval = TimeSpan.FromSeconds(5) };
    private readonly DispatcherTimer _overlayTimer = new() { Interval = TimeSpan.FromSeconds(3) };

    private AppSection _currentSection = AppSection.Home;
    private AppSection _lastNonPlayerSection = AppSection.Home;
    private LibraryInfo? _selectedLibrary;
    private string _currentFolder = "";
    private OnlineGroup? _selectedOnlineGroup;
    private ArcaSortOrder _currentSort = ArcaSortOrder.TitleAscending;
    private IReadOnlyList<MediaInfo> _currentVisibleMedia = [];
    private IReadOnlyList<MediaInfo> _currentSearchResults = [];
    private string? _currentMediaId;
    private string _currentTitle = "";
    private double _duration = -1;
    private double _position = -1;
    private double? _pendingResumeSeconds;
    private bool _updatingSliderFromEngine;
    private bool _updatingVolumeFromEngine;
    private bool _scrubbing;
    private bool _isUpdatingNavigation;

    public MainWindow(string? startupFile)
    {
        _startupFile = startupFile;
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(ShellTitleBar);
        Title = "Arca";
        UpdateTitleBarColors();

        VideoPanel.Loaded += OnPanelLoaded;
        Closed += OnClosed;

        PositionSlider.AddHandler(UIElement.PointerPressedEvent,
            new PointerEventHandler((_, _) => _scrubbing = true), true);
        PositionSlider.AddHandler(UIElement.PointerReleasedEvent,
            new PointerEventHandler((_, _) => _scrubbing = false), true);
        PositionSlider.AddHandler(UIElement.PointerCaptureLostEvent,
            new PointerEventHandler((_, _) => _scrubbing = false), true);

        _progressTimer.Tick += (_, _) => SaveProgress();
        _overlayTimer.Tick += (_, _) => SetPlayerOverlayVisible(false);

        SortComboBox.SelectedIndex = 0;
        ThemeComboBox.SelectedIndex = 0;
        NavigateTo(AppSection.Home);
    }

    private void OnPanelLoaded(object sender, RoutedEventArgs e)
    {
        if (_engine is not null)
            return;

        _engine = new PlayerEngine(DispatcherQueue);
        if (!_engine.AttachToPanel(VideoPanel, this))
        {
            PlayerIdleHint.Text = "Render initialization failed - see debug log.";
            return;
        }

        _engine.StateChanged += OnEngineState;
        _engine.PositionChanged += OnEnginePosition;
        _engine.DurationChanged += OnEngineDuration;
        _engine.FileLoaded += OnEngineFileLoaded;
        _engine.PlaybackError += msg =>
        {
            PlaybackErrorTextBlock.Text = $"Playback error: {msg}";
            PlaybackErrorTextBlock.Visibility = Visibility.Visible;
        };

        try
        {
            string dbPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Arca", "arca.db");
            _store = new LibraryStore(dbPath);
            _queue = _store.CreateQueue();
            RefreshLibraries();
            RefreshHome();
        }
        catch (Exception ex)
        {
            HomeStatusTextBlock.Text = $"Library DB unavailable: {ex.Message}";
            LibraryStatusTextBlock.Text = HomeStatusTextBlock.Text;
        }

        _progressTimer.Start();
        if (_startupFile is not null)
            LoadExternalFile(_startupFile);
    }

    private void RefreshLibraries(long? selectId = null)
    {
        if (_store is null)
            return;
        var libs = _store.Libraries().ToList();
        HomeLibrariesListView.ItemsSource = libs;
        LibraryRootGridView.ItemsSource = libs;
        HomeStatusTextBlock.Text = libs.Count == 0
            ? "Add a library or open a file to begin."
            : $"{libs.Count} librar{(libs.Count == 1 ? "y" : "ies")} available.";

        if (selectId is long id)
        {
            var lib = libs.FirstOrDefault(l => l.Id == id);
            if (lib is not null)
                OpenLibrary(lib);
        }
        else if (_selectedLibrary is not null)
        {
            _selectedLibrary = libs.FirstOrDefault(l => l.Id == _selectedLibrary.Id);
            if (_selectedLibrary is not null)
                LoadLibraryChildren();
        }
    }

    private void RefreshHome()
    {
        if (_store is null)
            return;
        ContinueWatchingListView.ItemsSource = _store.ContinueWatching(20);
    }

    private void ShowLibraryRoots()
    {
        _selectedLibrary = null;
        _selectedOnlineGroup = null;
        _currentFolder = "";
        _currentVisibleMedia = [];
        LibraryRootsPanel.Visibility = Visibility.Visible;
        LibraryExplorerPanel.Visibility = Visibility.Collapsed;
        LibraryTitleTextBlock.Text = "Video library";
        LibraryPathTextBlock.Text = "Choose a library.";
        LibraryStatusTextBlock.Text = "";
        BackToLibrariesButton.IsEnabled = false;
        SortComboBox.IsEnabled = false;
        RescanLibraryButton.IsEnabled = false;
    }

    private void OpenLibrary(LibraryInfo library)
    {
        _selectedLibrary = library;
        _selectedOnlineGroup = null;
        _currentFolder = "";
        LoadLibraryChildren();
        NavigateTo(AppSection.Library);
    }

    private void LoadLibraryChildren()
    {
        if (_store is null || _selectedLibrary is null)
        {
            ShowLibraryRoots();
            return;
        }

        var lib = _selectedLibrary;
        var children = _store.Children(lib.Id, _currentFolder, _currentSort);
        LibraryRootsPanel.Visibility = Visibility.Collapsed;
        LibraryExplorerPanel.Visibility = Visibility.Visible;
        BackToLibrariesButton.IsEnabled = true;
        SortComboBox.IsEnabled = true;
        RescanLibraryButton.IsEnabled = true;

        LibraryTitleTextBlock.Text = _selectedOnlineGroup?.Key ?? lib.Name;
        LibraryPathTextBlock.Text = lib.IsOnline
            ? $"{lib.RootPath} - online grouping scaffold"
            : string.IsNullOrWhiteSpace(_currentFolder) ? lib.RootPath : $"{lib.RootPath}\\{_currentFolder.Replace('/', '\\')}";
        LibraryBreadcrumbTextBlock.Text = string.IsNullOrWhiteSpace(_currentFolder) ? "All files" : _currentFolder;

        if (lib.IsOnline && string.IsNullOrWhiteSpace(_currentFolder) && _selectedOnlineGroup is null)
        {
            var groups = _store.Media(lib.Id)
                .GroupBy(m => string.IsNullOrWhiteSpace(m.GroupDisplay) ? m.DisplayTitle : m.GroupDisplay)
                .OrderBy(g => g.Key, StringComparer.CurrentCultureIgnoreCase)
                .Select(g => new OnlineGroup(g.Key, g.ToList()))
                .ToList();
            FoldersListView.ItemsSource = null;
            OnlineGroupsGridView.ItemsSource = groups;
            OnlineGroupsGridView.Visibility = groups.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            MediaGridView.Visibility = Visibility.Collapsed;
            _currentVisibleMedia = groups.SelectMany(g => g.Items).ToList();
            LibraryStatusTextBlock.Text = $"{lib.ModeLabel} - {groups.Count} group{(groups.Count == 1 ? "" : "s")} - {lib.ItemCount} files";
            LibraryEmptyStateTextBlock.Text = groups.Count == 0 ? "No grouped media yet." : "";
            return;
        }

        var media = _selectedOnlineGroup?.Items.ToList() ?? children.Media;
        sortMediaForShell(media);
        _currentVisibleMedia = media;
        FoldersListView.ItemsSource = _selectedOnlineGroup is null ? children.Folders : null;
        OnlineGroupsGridView.Visibility = Visibility.Collapsed;
        MediaGridView.Visibility = media.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        MediaGridView.ItemsSource = media;
        LibraryStatusTextBlock.Text = $"{lib.ModeLabel} - {children.Folders.Count} folders - {media.Count} videos";
        LibraryEmptyStateTextBlock.Text = media.Count == 0 && children.Folders.Count == 0 ? "No supported media files here." : "";

        void sortMediaForShell(List<MediaInfo> items)
        {
            Comparison<MediaInfo> comparison = _currentSort switch
            {
                ArcaSortOrder.TitleDescending => (a, b) => string.Compare(b.DisplayTitle, a.DisplayTitle, StringComparison.CurrentCultureIgnoreCase),
                ArcaSortOrder.AddedDescending => (a, b) => b.AddedUtc.CompareTo(a.AddedUtc),
                ArcaSortOrder.ModifiedDescending => (a, b) => b.ModifiedUtc.CompareTo(a.ModifiedUtc),
                ArcaSortOrder.SizeDescending => (a, b) => b.Size.CompareTo(a.Size),
                _ => (a, b) => string.Compare(a.DisplayTitle, b.DisplayTitle, StringComparison.CurrentCultureIgnoreCase),
            };
            items.Sort(comparison);
        }
    }

    private async void OnAddLibrary(object sender, RoutedEventArgs e)
    {
        if (_store is null)
            return;
        var picker = new FolderPicker();
        picker.FileTypeFilter.Add("*");
        WinRT.Interop.InitializeWithWindow.Initialize(
            picker, WinRT.Interop.WindowNative.GetWindowHandle(this));
        var folder = await picker.PickSingleFolderAsync();
        if (folder is null)
            return;

        var nameBox = new TextBox { Text = folder.Name, Header = "Name" };
        var offline = new RadioButton
        {
            Content = "Offline - file explorer, never goes online",
            IsChecked = true,
        };
        var online = new RadioButton
        {
            Content = "Online - local grouping now, metadata fetch later",
        };
        var dialog = new ContentDialog
        {
            Title = "Add library",
            PrimaryButtonText = "Add",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = RootGrid.XamlRoot,
            Content = new StackPanel
            {
                Spacing = 12,
                Children = { nameBox, offline, online },
            },
        };
        if (await dialog.ShowAsync() != ContentDialogResult.Primary)
            return;

        long id = _store.AddLibrary(nameBox.Text.Trim(), folder.Path, online.IsChecked == true);
        if (id < 0)
        {
            LibraryStatusTextBlock.Text = "Add failed (already imported?)";
            return;
        }
        LibraryStatusTextBlock.Text = "Scanning...";
        var (added, removed) = await Task.Run(() => _store.Scan(id));
        LibraryStatusTextBlock.Text = $"Scan: +{added} -{removed}";
        RefreshLibraries(id);
        RefreshHome();
    }

    private async void OnRescanLibrary(object sender, RoutedEventArgs e)
    {
        if (_store is null || _selectedLibrary is null)
            return;
        LibraryStatusTextBlock.Text = "Scanning...";
        var id = _selectedLibrary.Id;
        var (added, removed) = await Task.Run(() => _store.Scan(id));
        LibraryStatusTextBlock.Text = $"Scan: +{added} -{removed}";
        RefreshLibraries(id);
        RefreshHome();
    }

    private void RunSearch(string query)
    {
        if (_store is null)
            return;
        _currentSearchResults = string.IsNullOrWhiteSpace(query)
            ? []
            : _store.Search(query, 0, 80);
        SearchResultsListView.ItemsSource = _currentSearchResults;
        SearchHeaderTextBlock.Text = string.IsNullOrWhiteSpace(query) ? "Search" : $"Search: {query}";
        SearchScopeTextBlock.Text = "Across imported libraries";
        SearchEmptyStateTextBlock.Text = _currentSearchResults.Count == 0
            ? string.IsNullOrWhiteSpace(query) ? "Type in the title bar search box." : "No matches."
            : "";
        NavigateTo(AppSection.Search);
    }

    private void PlayMedia(MediaInfo media, IEnumerable<MediaInfo>? queueSource = null, bool tryResume = false)
    {
        if (_store is null || _queue is null)
            return;
        var queueItems = (queueSource ?? [media]).Where(m => !string.IsNullOrWhiteSpace(m.Id)).ToList();
        _queue.Set(queueItems.Select(m => m.Id), media.Id);
        LoadCurrentQueueItem(tryResume);
    }

    private void LoadCurrentQueueItem(bool tryResume)
    {
        if (_store is null || _queue is null || _engine is null)
            return;
        var current = _queue.Current;
        string? path = _queue.PathForCurrent();
        if (current is null || string.IsNullOrWhiteSpace(path))
            return;
        _currentMediaId = current.Id;
        _currentTitle = current.DisplayTitle;
        _pendingResumeSeconds = tryResume ? _store.ResumeSeconds(current.Id) : null;
        LoadFile(path, current.DisplayTitle);
        RefreshQueueView();
        NavigateTo(AppSection.Player);
    }

    private void LoadExternalFile(string path)
    {
        _queue?.Set([], null);
        _currentMediaId = null;
        _pendingResumeSeconds = null;
        LoadFile(path, Path.GetFileName(path));
        NavigateTo(AppSection.Player);
    }

    private void LoadFile(string path, string title)
    {
        if (_engine is null)
            return;
        _currentTitle = title;
        _duration = -1;
        _position = -1;
        PlayerTitleTextBlock.Text = title;
        PlayerIdleHint.Visibility = Visibility.Collapsed;
        PlaybackErrorTextBlock.Visibility = Visibility.Collapsed;
        _engine.Load(path);
    }

    private void RefreshQueueView()
    {
        if (_queue is null)
            return;
        var snapshot = _queue.Snapshot;
        QueueItemsListView.ItemsSource = snapshot.Items;
        QueueSummaryTextBlock.Text = snapshot.Items.Count == 0
            ? "No queued media."
            : $"{snapshot.Items.Count} item{(snapshot.Items.Count == 1 ? "" : "s")} - current {snapshot.CurrentIndex + 1}";
        QueueEmptyStateTextBlock.Text = snapshot.Items.Count == 0 ? "Play from the library or search to build a queue." : "";
        QueueButton.IsEnabled = snapshot.Items.Count > 0;
        PreviousButton.IsEnabled = snapshot.Items.Count > 1;
        NextButton.IsEnabled = snapshot.Items.Count > 1;
        ShuffleButton.Opacity = snapshot.Shuffle ? 1.0 : 0.65;
    }

    private void NavigateTo(AppSection section)
    {
        if (section != AppSection.Player && section != AppSection.Queue && section != AppSection.Settings)
            _lastNonPlayerSection = section;

        _currentSection = section;
        HomeView.Visibility = section == AppSection.Home ? Visibility.Visible : Visibility.Collapsed;
        LibraryView.Visibility = section == AppSection.Library ? Visibility.Visible : Visibility.Collapsed;
        SearchView.Visibility = section == AppSection.Search ? Visibility.Visible : Visibility.Collapsed;
        QueueView.Visibility = section == AppSection.Queue ? Visibility.Visible : Visibility.Collapsed;
        SettingsView.Visibility = section == AppSection.Settings ? Visibility.Visible : Visibility.Collapsed;
        PlayerView.Visibility = section == AppSection.Player ? Visibility.Visible : Visibility.Collapsed;
        ShellNavigationView.Visibility = section == AppSection.Player ? Visibility.Collapsed : Visibility.Visible;
        ShellTitleBar.Visibility = section == AppSection.Player ? Visibility.Collapsed : Visibility.Visible;

        _isUpdatingNavigation = true;
        ShellNavigationView.SelectedItem = section switch
        {
            AppSection.Home => HomeNavigationItem,
            AppSection.Library => LibraryNavigationItem,
            AppSection.Search => SearchNavigationItem,
            AppSection.Player => PlayerNavigationItem,
            AppSection.Queue => QueueNavigationItem,
            AppSection.Settings => SettingsNavigationItem,
            _ => HomeNavigationItem,
        };
        _isUpdatingNavigation = false;

        if (section == AppSection.Library && _selectedLibrary is null)
            ShowLibraryRoots();
        if (section == AppSection.Queue)
            RefreshQueueView();
    }

    private void OnEngineState(ArcaPlayState state)
    {
        PlayPauseButton.IsEnabled = state is not ArcaPlayState.Idle;
        PositionSlider.IsEnabled = state is ArcaPlayState.Playing or ArcaPlayState.Paused or ArcaPlayState.Ended;
        PlayPauseIcon.Symbol = state == ArcaPlayState.Playing ? Symbol.Pause : Symbol.Play;

        string status = state switch
        {
            ArcaPlayState.Idle => "Arca",
            ArcaPlayState.Loading => $"Arca - loading {_currentTitle}",
            ArcaPlayState.Playing => $"Arca - {_currentTitle}",
            ArcaPlayState.Paused => $"Arca - {_currentTitle} (paused)",
            ArcaPlayState.Ended => $"Arca - {_currentTitle} (ended)",
            _ => "Arca",
        };
        var info = _engine?.TargetInfo;
        if (info is { HdrActive: true } i && state == ArcaPlayState.Playing)
            status += $"  [HDR {i.DisplayMaxNits:F0} nits]";
        Title = status;
        ShellTitleBar.Title = status;
        PlayerTitleTextBlock.Text = status;

        if (state == ArcaPlayState.Ended)
        {
            SaveProgress(completed: true);
            if (_queue?.Next() == true)
                LoadCurrentQueueItem(tryResume: false);
        }
    }

    private void OnEnginePosition(double seconds)
    {
        if (seconds < 0)
            return;
        _position = seconds;
        if (!_scrubbing)
        {
            _updatingSliderFromEngine = true;
            PositionSlider.Value = seconds;
            _updatingSliderFromEngine = false;
        }
        UpdateTimeText();
    }

    private void OnEngineDuration(double seconds)
    {
        if (seconds <= 0)
            return;
        _duration = seconds;
        _updatingSliderFromEngine = true;
        PositionSlider.Maximum = seconds;
        _updatingSliderFromEngine = false;
        UpdateTimeText();
    }

    private void OnEngineFileLoaded()
    {
        System.Diagnostics.Debug.WriteLine(
            $"[arca] video-out-params: {_engine?.GetDiagnosticProperty("video-out-params")}");
        _updatingVolumeFromEngine = true;
        VolumeSlider.Value = _engine?.Volume ?? 100;
        _updatingVolumeFromEngine = false;
        if (_pendingResumeSeconds is double resume && resume > 0)
        {
            _engine?.SeekAbsolute(resume);
            _pendingResumeSeconds = null;
        }
    }

    private void SaveProgress(bool completed = false)
    {
        if (_store is null || _engine is null || string.IsNullOrWhiteSpace(_currentMediaId))
            return;
        double pos = _engine.Position;
        double dur = _engine.Duration;
        if (pos >= 0)
            _store.SaveProgress(_currentMediaId, pos, dur, completed);
        RefreshHome();
    }

    private void UpdateTimeText()
    {
        string pos = FormatTime(_position);
        string dur = _duration > 0 ? FormatTime(_duration) : "--:--";
        TimeTextBlock.Text = $"{pos} / {dur}";
    }

    private static string FormatTime(double seconds)
    {
        if (seconds < 0)
            return "--:--";
        var t = TimeSpan.FromSeconds(Math.Max(0, seconds));
        return t.TotalHours >= 1 ? t.ToString(@"h\:mm\:ss") : t.ToString(@"m\:ss");
    }

    private void SetPlayerOverlayVisible(bool visible)
    {
        if (_currentSection != AppSection.Player)
            return;
        TransportOverlay.Opacity = visible ? 1 : 0;
        PlayerTitleOverlay.Opacity = visible ? 1 : 0;
        if (visible)
        {
            _overlayTimer.Stop();
            _overlayTimer.Start();
        }
    }

    private void ToggleFullscreen()
    {
        bool isFull = AppWindow.Presenter.Kind == AppWindowPresenterKind.FullScreen;
        AppWindow.SetPresenter(isFull ? AppWindowPresenterKind.Default : AppWindowPresenterKind.FullScreen);
        if (!isFull)
            NavigateTo(AppSection.Player);
    }

    private void ToggleMute()
    {
        if (_engine is null)
            return;
        _engine.Muted = !_engine.Muted;
        MuteIcon.Symbol = _engine.Muted ? Symbol.Mute : Symbol.Volume;
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
            LoadExternalFile(file.Path);
    }

    private void OnExit(object sender, RoutedEventArgs e) => Close();
    private void OnPlayPause(object sender, RoutedEventArgs e) => _engine?.TogglePause();
    private void OnStopClicked(object sender, RoutedEventArgs e) => _engine?.Stop();
    private void OnSeekBackClicked(object sender, RoutedEventArgs e) => _engine?.SeekRelative(-10);
    private void OnSeekForwardClicked(object sender, RoutedEventArgs e) => _engine?.SeekRelative(10);
    private void OnToggleMute(object sender, RoutedEventArgs e) => ToggleMute();
    private void OnToggleFullscreenClicked(object sender, RoutedEventArgs e) => ToggleFullscreen();
    private void OnQueueButtonClicked(object sender, RoutedEventArgs e) => NavigateTo(AppSection.Queue);
    private void OnPlayerBackClicked(object sender, RoutedEventArgs e) => NavigateTo(_lastNonPlayerSection);
    private void OnPlayerPointerMoved(object sender, PointerRoutedEventArgs e) => SetPlayerOverlayVisible(true);

    private void OnPreviousClicked(object sender, RoutedEventArgs e)
    {
        if (_queue?.Previous() == true)
            LoadCurrentQueueItem(tryResume: false);
    }

    private void OnNextClicked(object sender, RoutedEventArgs e)
    {
        if (_queue?.Next() == true)
            LoadCurrentQueueItem(tryResume: false);
    }

    private void OnShuffleClicked(object sender, RoutedEventArgs e)
    {
        if (_queue is null)
            return;
        _queue.Shuffle = !_queue.Shuffle;
        RefreshQueueView();
    }

    private void OnSeekSliderChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_updatingSliderFromEngine || _engine is null)
            return;
        _engine.SeekAbsolute(e.NewValue);
    }

    private void OnVolumeSliderChanged(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (_updatingVolumeFromEngine || _engine is null)
            return;
        _engine.Volume = e.NewValue;
    }

    private void OnLibraryRootItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is LibraryInfo lib)
            OpenLibrary(lib);
    }

    private void OnHomeLibraryItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is LibraryInfo lib)
            OpenLibrary(lib);
    }

    private void OnFolderItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is FolderInfo folder)
        {
            _currentFolder = folder.RelPath;
            _selectedOnlineGroup = null;
            LoadLibraryChildren();
        }
    }

    private void OnOnlineGroupItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is OnlineGroup group)
        {
            _selectedOnlineGroup = group;
            LoadLibraryChildren();
        }
    }

    private void OnMediaItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is MediaInfo media)
            PlayMedia(media, _currentVisibleMedia, tryResume: true);
    }

    private void OnSearchResultItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is MediaInfo media)
            PlayMedia(media, _currentSearchResults, tryResume: true);
    }

    private void OnSearchResultPlayClicked(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string id } &&
            _currentSearchResults.FirstOrDefault(m => m.Id == id) is { } media)
            PlayMedia(media, _currentSearchResults, tryResume: true);
    }

    private void OnContinueWatchingItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is ProgressEntry entry)
            PlayMedia(entry.Media, [entry.Media], tryResume: true);
    }

    private void OnQueueItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is MediaInfo media && _queue?.MoveTo(media.Id) == true)
            LoadCurrentQueueItem(tryResume: false);
    }

    private void OnLibraryBackClicked(object sender, RoutedEventArgs e) => NavigateLibraryBack();

    private void NavigateLibraryBack()
    {
        if (_selectedOnlineGroup is not null)
        {
            _selectedOnlineGroup = null;
            LoadLibraryChildren();
            return;
        }
        if (!string.IsNullOrWhiteSpace(_currentFolder))
        {
            int slash = _currentFolder.LastIndexOf('/');
            _currentFolder = slash < 0 ? "" : _currentFolder[..slash];
            LoadLibraryChildren();
            return;
        }
        ShowLibraryRoots();
    }

    private void OnSortChanged(object sender, SelectionChangedEventArgs e)
    {
        if (SortComboBox.SelectedItem is not ComboBoxItem item || item.Tag is not string tag)
            return;
        _currentSort = tag switch
        {
            "TitleDescending" => ArcaSortOrder.TitleDescending,
            "AddedDescending" => ArcaSortOrder.AddedDescending,
            "ModifiedDescending" => ArcaSortOrder.ModifiedDescending,
            "SizeDescending" => ArcaSortOrder.SizeDescending,
            _ => ArcaSortOrder.TitleAscending,
        };
        if (_selectedLibrary is not null)
            LoadLibraryChildren();
    }

    private void OnGlobalSearchSubmitted(AutoSuggestBox sender, AutoSuggestBoxQuerySubmittedEventArgs args) =>
        RunSearch(sender.Text.Trim());

    private void OnGlobalSearchTextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
    {
        if (args.Reason != AutoSuggestionBoxTextChangeReason.UserInput)
            return;
        string query = sender.Text.Trim();
        if (query.Length == 0 || query.Length >= 2)
            RunSearch(query);
    }

    private void OnNavigationSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (_isUpdatingNavigation || args.SelectedItem is not NavigationViewItem item || item.Tag is not string tag)
            return;
        NavigateTo(tag switch
        {
            "Library" => AppSection.Library,
            "Search" => AppSection.Search,
            "Player" => AppSection.Player,
            "Queue" => AppSection.Queue,
            "Settings" => AppSection.Settings,
            _ => AppSection.Home,
        });
    }

    private void OnTitleBarBackRequested(TitleBar sender, object args)
    {
        if (_currentSection == AppSection.Library)
            NavigateLibraryBack();
        else if (_currentSection == AppSection.Search)
            NavigateTo(_selectedLibrary is null ? AppSection.Home : AppSection.Library);
        else if (_currentSection == AppSection.Queue || _currentSection == AppSection.Settings)
            NavigateTo(_lastNonPlayerSection);
        else
            NavigateTo(AppSection.Home);
    }

    private void OnThemeMenuClicked(object sender, RoutedEventArgs e)
    {
        if (sender is ToggleMenuFlyoutItem { Tag: string theme })
            ApplyTheme(theme);
    }

    private void OnThemeComboBoxChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ThemeComboBox.SelectedItem is ComboBoxItem { Tag: string theme })
            ApplyTheme(theme);
    }

    private void ApplyTheme(string theme)
    {
        RootGrid.RequestedTheme = theme switch
        {
            "Light" => ElementTheme.Light,
            "Dark" => ElementTheme.Dark,
            _ => ElementTheme.Default,
        };
        ThemeLightMenuItem.IsChecked = RootGrid.RequestedTheme == ElementTheme.Light;
        ThemeDarkMenuItem.IsChecked = RootGrid.RequestedTheme == ElementTheme.Dark;
        UpdateTitleBarColors();
    }

    private void UpdateTitleBarColors()
    {
        if (!AppWindowTitleBar.IsCustomizationSupported())
            return;
        var titleBar = AppWindow.TitleBar;
        titleBar.ButtonBackgroundColor = Colors.Transparent;
        titleBar.ButtonInactiveBackgroundColor = Colors.Transparent;
        titleBar.ButtonHoverBackgroundColor = ColorHelper.FromArgb(40, 127, 127, 127);
        titleBar.ButtonPressedBackgroundColor = ColorHelper.FromArgb(60, 127, 127, 127);
    }

    private void OnPanelSizeChanged(object sender, SizeChangedEventArgs e) =>
        _engine?.ResizePanel(e.NewSize.Width, e.NewSize.Height,
                             VideoPanel.CompositionScaleX, VideoPanel.CompositionScaleY);

    private void OnPanelScaleChanged(SwapChainPanel sender, object args) =>
        _engine?.ResizePanel(sender.ActualWidth, sender.ActualHeight,
                             sender.CompositionScaleX, sender.CompositionScaleY);

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
    private void OnAccelNext(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (_queue?.Next() == true) LoadCurrentQueueItem(false); a.Handled = true; }
    private void OnAccelPrevious(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (_queue?.Previous() == true) LoadCurrentQueueItem(false); a.Handled = true; }
    private void OnAccelQueue(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { NavigateTo(AppSection.Queue); a.Handled = true; }
    private void OnAccelShuffle(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (_queue is not null) { _queue.Shuffle = !_queue.Shuffle; RefreshQueueView(); } a.Handled = true; }
    private void OnAccelToggleFullscreen(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { ToggleFullscreen(); a.Handled = true; }
    private void OnAccelEscape(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { if (AppWindow.Presenter.Kind == AppWindowPresenterKind.FullScreen) ToggleFullscreen(); else if (_currentSection == AppSection.Player) NavigateTo(_lastNonPlayerSection); a.Handled = true; }
    private void OnAccelOpenFile(KeyboardAccelerator s, KeyboardAcceleratorInvokedEventArgs a)
    { OnOpenFile(this, new RoutedEventArgs()); a.Handled = true; }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        SaveProgress();
        _progressTimer.Stop();
        _overlayTimer.Stop();
        _queue?.Dispose();
        _queue = null;
        _engine?.Dispose();
        _engine = null;
        _store?.Dispose();
        _store = null;
    }
}
