// Managed wrappers over the arca library ABI (arca_library.h). List/search/
// queue payloads cross as UTF-8 JSON; models deserialize with System.Text.Json.

using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Arca.Interop;

public enum ArcaSortOrder
{
    TitleAscending = 0,
    TitleDescending = 1,
    AddedDescending = 2,
    AddedAscending = 3,
    ModifiedDescending = 4,
    ModifiedAscending = 5,
    SizeDescending = 6,
    SizeAscending = 7,
}

public sealed record LibraryInfo
{
    [JsonPropertyName("id")] public long Id { get; init; }
    [JsonPropertyName("name")] public string Name { get; init; } = "";
    [JsonPropertyName("rootPath")] public string RootPath { get; init; } = "";
    [JsonPropertyName("mode")] public string Mode { get; init; } = "offline";
    [JsonPropertyName("itemCount")] public long ItemCount { get; init; }

    [JsonIgnore] public bool IsOnline => Mode == "online";
    [JsonIgnore] public string Display => $"{Name} ({ItemCount})";
    [JsonIgnore] public string ModeLabel => IsOnline ? "Online" : "Offline";
}

public sealed record FolderInfo
{
    [JsonPropertyName("name")] public string Name { get; init; } = "";
    [JsonPropertyName("relPath")] public string RelPath { get; init; } = "";
    [JsonPropertyName("itemCount")] public long ItemCount { get; init; }

    [JsonIgnore] public string Display => $"{Name} ({ItemCount})";
}

public sealed record MediaInfo
{
    [JsonPropertyName("id")] public string Id { get; init; } = "";
    [JsonPropertyName("fileName")] public string FileName { get; init; } = "";
    [JsonPropertyName("relPath")] public string RelPath { get; init; } = "";
    [JsonPropertyName("folderRelPath")] public string FolderRelPath { get; init; } = "";
    [JsonPropertyName("size")] public long Size { get; init; }
    [JsonPropertyName("modifiedUtc")] public long ModifiedUtc { get; init; }
    [JsonPropertyName("addedUtc")] public long AddedUtc { get; init; }
    [JsonPropertyName("title")] public string? Title { get; init; }
    [JsonPropertyName("year")] public int? Year { get; init; }
    [JsonPropertyName("season")] public int? Season { get; init; }
    [JsonPropertyName("episode")] public int? Episode { get; init; }
    [JsonPropertyName("groupKey")] public string? GroupKey { get; init; }
    [JsonPropertyName("libraryId")] public long LibraryId { get; init; }
    [JsonPropertyName("libraryName")] public string? LibraryName { get; init; }
    [JsonPropertyName("mode")] public string? Mode { get; init; }
    [JsonPropertyName("isCurrent")] public bool IsCurrent { get; init; }
    [JsonPropertyName("thumbnailPath")] public string? ThumbnailPath { get; init; }
    [JsonPropertyName("durationSeconds")] public double? DurationSeconds { get; init; }
    [JsonPropertyName("resolution")] public string? Resolution { get; init; }
    [JsonPropertyName("dynamicRange")] public string? DynamicRange { get; init; }
    [JsonPropertyName("probeStatus")] public string? ProbeStatus { get; init; }

    [JsonIgnore]
    public string DisplayTitle
    {
        get
        {
            if (Season is int s && Episode is int e)
                return $"S{s:D2}E{e:D2}  {Title ?? FileName}";
            if (!string.IsNullOrWhiteSpace(Title))
                return Year is int y && Season is null ? $"{Title} ({y})" : Title!;
            return Path.GetFileNameWithoutExtension(FileName);
        }
    }

    [JsonIgnore]
    public string Subtitle
    {
        get
        {
            string scope = string.IsNullOrWhiteSpace(LibraryName) ? RelPath : $"{LibraryName} - {RelPath}";
            string technical = !string.IsNullOrWhiteSpace(Resolution)
                ? $" - {Resolution}{(!string.IsNullOrWhiteSpace(DynamicRange) ? $" {DynamicRange}" : "")}"
                : "";
            return $"{scope} - {FormatFileSize(Size)}{technical}";
        }
    }

    [JsonIgnore]
    public Uri? ThumbnailUri =>
        !string.IsNullOrWhiteSpace(ThumbnailPath) && File.Exists(ThumbnailPath)
            ? new Uri(ThumbnailPath)
            : null;

    [JsonIgnore]
    public string GroupDisplay =>
        !string.IsNullOrWhiteSpace(Title)
            ? Year is int y && Season is null ? $"{Title} ({y})" : Title!
            : "";

    public static string FormatFileSize(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB", "TB"];
        double value = Math.Max(0, bytes);
        int unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        return unit == 0 ? $"{value:0} {units[unit]}" : $"{value:0.0} {units[unit]}";
    }
}

public sealed record MediaProbeInfo
{
    [JsonPropertyName("status")] public string Status { get; init; } = "missing";
    [JsonPropertyName("error")] public string? Error { get; init; }
    [JsonPropertyName("durationSeconds")] public double? DurationSeconds { get; init; }
    [JsonPropertyName("width")] public int? Width { get; init; }
    [JsonPropertyName("height")] public int? Height { get; init; }
    [JsonPropertyName("frameRate")] public string? FrameRate { get; init; }
    [JsonPropertyName("videoCodec")] public string? VideoCodec { get; init; }
    [JsonPropertyName("audioCodec")] public string? AudioCodec { get; init; }
    [JsonPropertyName("pixelFormat")] public string? PixelFormat { get; init; }
    [JsonPropertyName("colorSpace")] public string? ColorSpace { get; init; }
    [JsonPropertyName("colorTransfer")] public string? ColorTransfer { get; init; }
    [JsonPropertyName("colorPrimaries")] public string? ColorPrimaries { get; init; }
    [JsonPropertyName("dynamicRange")] public string? DynamicRange { get; init; }
    [JsonPropertyName("bitrate")] public long? Bitrate { get; init; }

    [JsonIgnore]
    public string DurationLabel =>
        DurationSeconds is double seconds && seconds > 0
            ? TimeSpan.FromSeconds(seconds).ToString(seconds >= 3600 ? @"h\:mm\:ss" : @"m\:ss")
            : "Unknown";

    [JsonIgnore]
    public string ResolutionLabel =>
        Width is int w && Height is int h ? $"{w}x{h}" : "Unknown";

    [JsonIgnore]
    public string VideoSummary =>
        string.Join(" - ", new[] { VideoCodec, ResolutionLabel, FrameRate, DynamicRange }
            .Where(value => !string.IsNullOrWhiteSpace(value) && value != "Unknown"));

    [JsonIgnore]
    public string AudioSummary => string.IsNullOrWhiteSpace(AudioCodec) ? "Unknown" : AudioCodec!;

    [JsonIgnore]
    public string ColorSummary =>
        string.Join(" / ", new[] { ColorPrimaries, ColorTransfer, ColorSpace }
            .Where(value => !string.IsNullOrWhiteSpace(value)));

    [JsonIgnore]
    public string BitrateLabel =>
        Bitrate is long b && b > 0 ? $"{b / 1_000_000.0:0.0} Mbps" : "Unknown";
}

public sealed record MediaDetail
{
    [JsonPropertyName("media")] public MediaInfo Media { get; init; } = new();
    [JsonPropertyName("absolutePath")] public string AbsolutePath { get; init; } = "";
    [JsonPropertyName("probe")] public MediaProbeInfo Probe { get; init; } = new();
    [JsonPropertyName("thumbnails")] public List<string> Thumbnails { get; init; } = [];
    [JsonPropertyName("resumeSeconds")] public double? ResumeSeconds { get; init; }

    [JsonIgnore]
    public Uri? PrimaryThumbnailUri
    {
        get
        {
            string? path = Thumbnails.FirstOrDefault(File.Exists) ?? Media.ThumbnailPath;
            return !string.IsNullOrWhiteSpace(path) && File.Exists(path) ? new Uri(path) : null;
        }
    }

    [JsonIgnore]
    public string PathLabel => string.IsNullOrWhiteSpace(AbsolutePath) ? Media.RelPath : AbsolutePath;

    [JsonIgnore]
    public string ResumeLabel =>
        ResumeSeconds is double seconds && seconds > 0
            ? $"Resume at {TimeSpan.FromSeconds(seconds):h\\:mm\\:ss}"
            : "Play from start";
}

public sealed record MediaToolsStatus
{
    [JsonPropertyName("ffprobePath")] public string FfprobePath { get; init; } = "";
    [JsonPropertyName("ffprobeAvailable")] public bool FfprobeAvailable { get; init; }
    [JsonPropertyName("ffmpegPath")] public string FfmpegPath { get; init; } = "";
    [JsonPropertyName("ffmpegAvailable")] public bool FfmpegAvailable { get; init; }
    [JsonPropertyName("cacheRoot")] public string CacheRoot { get; init; } = "";

    [JsonIgnore]
    public string Summary =>
        $"ffprobe {(FfprobeAvailable ? "ready" : "missing")} - ffmpeg {(FfmpegAvailable ? "ready" : "missing")}";
}

public sealed record LibraryChildren
{
    [JsonPropertyName("folderRelPath")] public string FolderRelPath { get; init; } = "";
    [JsonPropertyName("folders")] public List<FolderInfo> Folders { get; init; } = [];
    [JsonPropertyName("media")] public List<MediaInfo> Media { get; init; } = [];
}

public sealed record ProgressEntry
{
    [JsonPropertyName("media")] public MediaInfo Media { get; init; } = new();
    [JsonPropertyName("positionSeconds")] public double PositionSeconds { get; init; }
    [JsonPropertyName("durationSeconds")] public double DurationSeconds { get; init; }
    [JsonPropertyName("lastUpdatedUtc")] public long LastUpdatedUtc { get; init; }

    [JsonIgnore]
    public string ProgressLabel =>
        DurationSeconds > 0
            ? $"{TimeSpan.FromSeconds(PositionSeconds):m\\:ss} / {TimeSpan.FromSeconds(DurationSeconds):m\\:ss}"
            : TimeSpan.FromSeconds(PositionSeconds).ToString(@"m\:ss");
}

public sealed record QueueSnapshot
{
    [JsonPropertyName("currentIndex")] public int CurrentIndex { get; init; }
    [JsonPropertyName("shuffle")] public bool Shuffle { get; init; }
    [JsonPropertyName("items")] public List<MediaInfo> Items { get; init; } = [];
}

public sealed record BrowseFilter
{
    [JsonPropertyName("key")] public string Key { get; init; } = "all";
    [JsonPropertyName("name")] public string Name { get; init; } = "All";
    [JsonPropertyName("count")] public long Count { get; init; }
    [JsonPropertyName("selected")] public bool IsSelected { get; init; }

    [JsonIgnore] public string Display => $"{Name} ({Count})";
}

public sealed record BrowseRow
{
    [JsonPropertyName("title")] public string Title { get; init; } = "";
    [JsonPropertyName("entries")] public List<MediaInfo> Entries { get; init; } = [];
}

public sealed record BrowseSection
{
    [JsonPropertyName("kind")] public string Kind { get; init; } = "";
    [JsonPropertyName("title")] public string Title { get; init; } = "";
    [JsonPropertyName("rows")] public List<BrowseRow> Rows { get; init; } = [];
}

public sealed record BrowseResult
{
    [JsonPropertyName("selectedFilter")] public string SelectedFilter { get; init; } = "all";
    [JsonPropertyName("filters")] public List<BrowseFilter> Filters { get; init; } = [];
    [JsonPropertyName("sections")] public List<BrowseSection> Sections { get; init; } = [];
}

public sealed class LibraryStore : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private IntPtr _db;

    public LibraryStore(string dbPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(dbPath)!);
        _db = arca_db_open(dbPath);
        if (_db == IntPtr.Zero)
            throw new InvalidOperationException($"arca_db_open failed: {dbPath}");
    }

    internal IntPtr Handle => _db;

    public long AddLibrary(string name, string rootPath, bool online) =>
        arca_library_add(_db, name, rootPath, online ? 1 : 0);

    public bool RemoveLibrary(long id) => arca_library_remove(_db, id) == ArcaStatus.Ok;

    public (int Added, int Removed) Scan(long libraryId)
    {
        arca_library_scan(_db, libraryId, out int added, out int removed);
        return (added, removed);
    }

    public IReadOnlyList<LibraryInfo> Libraries() =>
        FromJsonArray<LibraryInfo>(arca_library_list_json(_db));

    public IReadOnlyList<MediaInfo> Media(long libraryId) =>
        FromJsonArray<MediaInfo>(arca_media_list_json(_db, libraryId));

    public LibraryChildren Children(long libraryId, string folderRelPath, ArcaSortOrder sort) =>
        FromJsonObject<LibraryChildren>(arca_library_children_json(_db, libraryId, folderRelPath, sort)) ?? new();

    public IReadOnlyList<MediaInfo> Search(string query, long libraryId = 0, int limit = 80) =>
        FromJsonArray<MediaInfo>(arca_media_search_json(_db, query, libraryId, limit));

    public BrowseResult Browse(string filter = "all", int rowLimit = 8, int itemLimit = 24) =>
        FromJsonObject<BrowseResult>(arca_media_browse_json(_db, filter, rowLimit, itemLimit)) ?? new();

    public bool SaveProgress(string mediaId, double positionSeconds, double durationSeconds, bool completed) =>
        arca_progress_save(_db, mediaId, positionSeconds, durationSeconds, completed) == ArcaStatus.Ok;

    public double ResumeSeconds(string mediaId) => arca_progress_resume_seconds(_db, mediaId);

    public IReadOnlyList<ProgressEntry> ContinueWatching(int limit = 20) =>
        FromJsonArray<ProgressEntry>(arca_progress_continue_watching_json(_db, limit));

    public MediaDetail? MediaDetail(string mediaId) =>
        FromJsonObject<MediaDetail>(arca_media_detail_json(_db, mediaId));

    public bool Probe(string mediaId, bool generateThumbnails = true) =>
        arca_media_probe(_db, mediaId, generateThumbnails) == ArcaStatus.Ok;

    public (int Probed, int Failed) ProbeMissing(long libraryId, int limit = 24)
    {
        arca_library_probe_missing(_db, libraryId, limit, out int probed, out int failed);
        return (probed, failed);
    }

    public MediaToolsStatus? ToolsStatus() =>
        FromJsonObject<MediaToolsStatus>(arca_media_tools_status_json(_db));

    public PlaybackQueue CreateQueue() => new(this);

    public string? PathFor(string mediaId)
    {
        IntPtr p = arca_media_get_path(_db, mediaId);
        if (p == IntPtr.Zero)
            return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { NativeMethods.arca_string_free(p); }
    }

    internal static List<T> FromJsonArray<T>(IntPtr json)
    {
        if (json == IntPtr.Zero)
            return [];
        try
        {
            string s = Marshal.PtrToStringUTF8(json) ?? "[]";
            return JsonSerializer.Deserialize<List<T>>(s, JsonOptions) ?? [];
        }
        finally
        {
            NativeMethods.arca_string_free(json);
        }
    }

    internal static T? FromJsonObject<T>(IntPtr json)
    {
        if (json == IntPtr.Zero)
            return default;
        try
        {
            string s = Marshal.PtrToStringUTF8(json) ?? "{}";
            return JsonSerializer.Deserialize<T>(s, JsonOptions);
        }
        finally
        {
            NativeMethods.arca_string_free(json);
        }
    }

    public void Dispose()
    {
        if (_db != IntPtr.Zero)
        {
            arca_db_close(_db);
            _db = IntPtr.Zero;
        }
    }

    private const string Dll = "arca_core";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_db_open([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void arca_db_close(IntPtr db);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern long arca_library_add(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string rootPath, int mode);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_library_remove(IntPtr db, long id);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_library_list_json(IntPtr db);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_library_scan(
        IntPtr db, long id, out int added, out int removed);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_list_json(IntPtr db, long libraryId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_library_children_json(
        IntPtr db, long libraryId, [MarshalAs(UnmanagedType.LPUTF8Str)] string folderRelPath, ArcaSortOrder sort);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_search_json(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string query, long libraryId, int limit);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_browse_json(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string filter, int rowLimit, int itemLimit);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_get_path(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_media_probe(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId,
        [MarshalAs(UnmanagedType.I1)] bool generateThumbnails);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_library_probe_missing(
        IntPtr db, long libraryId, int limit, out int probed, out int failed);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_detail_json(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_media_tools_status_json(IntPtr db);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_progress_save(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId,
        double positionSeconds, double durationSeconds,
        [MarshalAs(UnmanagedType.I1)] bool isCompleted);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern double arca_progress_resume_seconds(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_progress_continue_watching_json(IntPtr db, int limit);
}

public sealed class PlaybackQueue : IDisposable
{
    private readonly LibraryStore _store;
    private IntPtr _queue;

    internal PlaybackQueue(LibraryStore store)
    {
        _store = store;
        _queue = arca_queue_create(store.Handle);
        if (_queue == IntPtr.Zero)
            throw new InvalidOperationException("arca_queue_create failed");
    }

    public QueueSnapshot Snapshot =>
        LibraryStore.FromJsonObject<QueueSnapshot>(arca_queue_list_json(_queue)) ?? new();

    public MediaInfo? Current =>
        LibraryStore.FromJsonObject<MediaInfo>(arca_queue_current_json(_queue));

    public string? CurrentId
    {
        get
        {
            IntPtr p = arca_queue_current_media_id(_queue);
            if (p == IntPtr.Zero)
                return null;
            try { return Marshal.PtrToStringUTF8(p); }
            finally { NativeMethods.arca_string_free(p); }
        }
    }

    public bool Shuffle
    {
        get => arca_queue_shuffle(_queue);
        set => arca_queue_set_shuffle(_queue, value);
    }

    public void Set(IEnumerable<string> mediaIds, string? currentMediaId)
    {
        string json = JsonSerializer.Serialize(mediaIds);
        arca_queue_set_from_media_ids_json(_queue, json, currentMediaId ?? "");
    }

    public bool MoveTo(string mediaId) => arca_queue_set_current(_queue, mediaId) == ArcaStatus.Ok;
    public bool Next() => arca_queue_next(_queue) == ArcaStatus.Ok;
    public bool Previous() => arca_queue_previous(_queue) == ArcaStatus.Ok;
    public string? PathForCurrent() => CurrentId is { } id ? _store.PathFor(id) : null;

    public void Dispose()
    {
        if (_queue != IntPtr.Zero)
        {
            arca_queue_destroy(_queue);
            _queue = IntPtr.Zero;
        }
    }

    private const string Dll = "arca_core";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_queue_create(IntPtr db);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void arca_queue_destroy(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_queue_set_from_media_ids_json(
        IntPtr queue, [MarshalAs(UnmanagedType.LPUTF8Str)] string idsJson,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string currentMediaId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_queue_list_json(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_queue_current_json(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr arca_queue_current_media_id(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_queue_set_current(
        IntPtr queue, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_queue_next(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_queue_previous(IntPtr queue);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern ArcaStatus arca_queue_set_shuffle(
        IntPtr queue, [MarshalAs(UnmanagedType.I1)] bool enabled);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    private static extern bool arca_queue_shuffle(IntPtr queue);
}
