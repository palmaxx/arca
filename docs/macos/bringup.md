# macOS scaffold bring-up

The macOS shell lives at `shells/macos/ArcaMac` as a SwiftPM SwiftUI app. It is
stub-first so it can be opened on a Mac before the native macOS render backend
is ready.

## First Check On A Mac

```bash
cd shells/macos/ArcaMac
swift run
```

Expected result: a native macOS window opens with a sidebar, library/search
views, a queue page, settings, and a player placeholder. This does not require
`arca_core` or Metal.

## Native Core Bridge

The Swift shell talks through `ArcaCoreClient`. Today the default factory uses
`StubArcaCoreClient`. `NativeArcaCoreClient.swift` is gated behind
`ARCA_NATIVE_CORE` and imports `CArcaCore`, whose module map points at
`core/include/arca`.

When the macOS core binary exists:

1. Build `arca_core` for macOS as a dynamic library.
2. Add the library search path and link flag to `Package.swift` or a local
   Xcode scheme.
3. Build with `-DARCA_NATIVE_CORE`.
4. Confirm library list, child browsing, search, continue-watching, and queue
   JSON decode before starting the Metal surface work.

## Manual Visual Checks

- Sidebar switches between Home, Library, Search, Player, Queue, Settings.
- Library rows select a root and folders drill in/out.
- Search returns stub results and selecting a result opens Player.
- Player remains a placeholder until the Metal surface is wired.
