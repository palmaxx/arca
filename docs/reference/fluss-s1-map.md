# Fluss sprint parity map

Reference: `C:\DEV\VS2026\Fluss`.

This sprint uses Fluss as the Windows UI and behavior reference while moving
stateful behavior into ARCA's C++ core.

## Ported Into Core

- Library child browsing: shells request immediate folders/media instead of
  walking paths themselves.
- Search: FTS5 media search through `arca_media_search_json`.
- Browse: filter/rail model through `arca_media_browse_json` so shells do not
  invent separate catalog grouping.
- Watch progress: save, resume thresholding, and continue-watching lists.
- Playback queue: in-memory queue with current item, next/previous, and
  shuffle state.

## Reused In Windows Shell Shape

- Custom title bar with menu and global search.
- Left navigation with Home, Browse, Library, Search, Player, Queue, and
  Settings.
- Browse page with filter chips and horizontal media rows.
- Library page split between roots, folders, grouped online media, and media
  rows.
- Player page with focused video surface and transport/title chrome as overlays
  over the render surface.

## Deferred

- Online fetcher and artwork download.
- Persistent queue/history beyond current watch progress.
- Tags, profile-specific settings, detail pages, thumbnail seek, and rich
  metadata pages.
- macOS MoltenVK/Vulkan surface and native player backend validation.
- Video controls polish beyond overlay placement.

## Sprint Validation

- Core build succeeds.
- `lib-verify.exe` passes. This is a core/mpv-render-context-style golden gate
  for ABI behavior, not a UI validation script.
- WinUI shell builds and can be visually checked by opening the app.
- macOS SwiftUI package exists and can run in stub mode on a Mac without
  MoltenVK.
