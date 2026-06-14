# Core ABI shell contract

ARCA shells are presentation layers. Playback state that must survive a UI
rewrite, platform change, or future automation belongs behind the C++ core's
flat C ABI.

## Ownership

- Functions returning `char *` return UTF-8 JSON or UTF-8 strings allocated by
  the core. The caller must free them with `arca_string_free`.
- Opaque handles (`arca_db`, `arca_queue`, future player/session handles) are
  created and destroyed by matching `arca_*_create/open` and `arca_*_destroy`
  calls. Shells never inspect their internals.
- Long-running calls such as `arca_library_scan` are synchronous by design and
  must be invoked off the UI thread by the shell.

## Library JSON

The shell-facing library model is intentionally denormalized:

- `arca_library_list_json`: root library list with `id`, `name`, `rootPath`,
  `mode`, and `itemCount`.
- `arca_library_children_json`: immediate folder + media children for explorer
  panes. This keeps WinUI and SwiftUI from reimplementing path grouping.
- `arca_media_search_json`: FTS5-backed media search. Empty queries return an
  empty list.
- Media objects include stable `id`, `fileName`, `relPath`, `folderRelPath`,
  size/timestamps, library identity, mode, and optional online parse fields.

## Progress And Queue

Resume and queue behavior is core-owned:

- `arca_progress_save`, `arca_progress_resume_seconds`, and
  `arca_progress_continue_watching_json` own watch progress and resume
  filtering.
- `arca_queue_*` owns current item, order, and shuffle. Shells may display and
  command the queue, but should not fork that logic.

This mirrors the useful Fluss behavior while avoiding a C#-only state model.

## Validation

Use simple checks:

- `build.ps1` for the native core and tools.
- `lib-verify.exe` for library/search/progress/queue ABI coverage.
- Windows shell `dotnet build` for P/Invoke and XAML compile coverage.
- macOS scaffold: `swift run` on a Mac for the stub shell; enable the native
  client only after `arca_core` is built and linked for macOS.
