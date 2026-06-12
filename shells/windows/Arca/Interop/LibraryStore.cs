// Managed wrapper over the arca library ABI (arca_library.h). List payloads
// cross as JSON; models deserialize with System.Text.Json.

using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Arca.Interop;

public sealed record LibraryInfo
{
    [JsonPropertyName("id")] public long Id { get; init; }
    [JsonPropertyName("name")] public string Name { get; init; } = "";
    [JsonPropertyName("rootPath")] public string RootPath { get; init; } = "";
    [JsonPropertyName("mode")] public string Mode { get; init; } = "offline";
    [JsonPropertyName("itemCount")] public long ItemCount { get; init; }

    [JsonIgnore] public bool IsOnline => Mode == "online";
    [JsonIgnore] public string Display => $"{Name}  ({ItemCount})";
    // Folder glyph for offline (file-explorer semantics), globe for online.
    [JsonIgnore] public string ModeGlyph => IsOnline ? "" : "";
}

public sealed record MediaInfo
{
    [JsonPropertyName("id")] public string Id { get; init; } = "";
    [JsonPropertyName("fileName")] public string FileName { get; init; } = "";
    [JsonPropertyName("relPath")] public string RelPath { get; init; } = "";
    [JsonPropertyName("size")] public long Size { get; init; }
    [JsonPropertyName("title")] public string? Title { get; init; }
    [JsonPropertyName("year")] public int? Year { get; init; }
    [JsonPropertyName("season")] public int? Season { get; init; }
    [JsonPropertyName("episode")] public int? Episode { get; init; }
    [JsonPropertyName("groupKey")] public string? GroupKey { get; init; }

    [JsonIgnore]
    public string Display =>
        Season is int s && Episode is int e
            ? $"S{s:D2}E{e:D2}  {FileName}"
            : RelPath;

    [JsonIgnore]
    public string GroupDisplay =>
        Title is null ? "" : Year is int y && Season is null ? $"{Title} ({y})" : Title;
}

public sealed class LibraryStore : IDisposable
{
    private IntPtr _db;

    public LibraryStore(string dbPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(dbPath)!);
        _db = arca_db_open(dbPath);
        if (_db == IntPtr.Zero)
            throw new InvalidOperationException($"arca_db_open failed: {dbPath}");
    }

    public long AddLibrary(string name, string rootPath, bool online) =>
        arca_library_add(_db, name, rootPath, online ? 1 : 0);

    public bool RemoveLibrary(long id) => arca_library_remove(_db, id) == ArcaStatus.Ok;

    public (int Added, int Removed) Scan(long libraryId)
    {
        arca_library_scan(_db, libraryId, out int added, out int removed);
        return (added, removed);
    }

    public IReadOnlyList<LibraryInfo> Libraries() =>
        FromJson<LibraryInfo>(arca_library_list_json(_db));

    public IReadOnlyList<MediaInfo> Media(long libraryId) =>
        FromJson<MediaInfo>(arca_media_list_json(_db, libraryId));

    public string? PathFor(string mediaId)
    {
        IntPtr p = arca_media_get_path(_db, mediaId);
        if (p == IntPtr.Zero)
            return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { NativeMethods.arca_string_free(p); }
    }

    private static List<T> FromJson<T>(IntPtr json)
    {
        if (json == IntPtr.Zero)
            return [];
        try
        {
            string s = Marshal.PtrToStringUTF8(json) ?? "[]";
            return JsonSerializer.Deserialize<List<T>>(s) ?? [];
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
    private static extern IntPtr arca_media_get_path(
        IntPtr db, [MarshalAs(UnmanagedType.LPUTF8Str)] string mediaId);
}
