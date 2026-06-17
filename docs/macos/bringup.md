# macOS scaffold bring-up

The macOS shell lives at `shells/macos/ArcaMac` as a SwiftPM SwiftUI app. It is
stub-first so it can be opened on a Mac before the native macOS render backend
is ready.

The render backend target is Vulkan over MoltenVK, not a native Metal
libplacebo backend. The sibling `C:\DEV\ai-dev\projects\mpv-mac-metal` notes
correct the earlier premise: libplacebo has no native Metal `pl_gpu`; mpv's
macOS graphics path is Vulkan presented through a Metal surface.

## First Check On A Mac

```bash
cd shells/macos/ArcaMac
swift run
```

Expected result: a native macOS window opens with a sidebar, library/search
views, Browse, a queue page, settings, and a player placeholder. This does not
require `arca_core`, MoltenVK, or a render backend.

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
4. Confirm library list, Browse, child browsing, search, continue-watching,
   and queue JSON decode before starting render-surface work.

## Render Backend Direction

Use the mpv fork's existing `MPV_RENDER_API_TYPE_PL_VULKAN` path:
`include/mpv/render_vulkan.h` and
`video/out/vulkan/libmpv_pl_vulkan.c`. On macOS the host must create/provide the
Vulkan device through MoltenVK and present through a `CAMetalLayer`/Metal
surface (`video/out/vulkan/context_mac.m` is the mpv windowed reference).

Day-0-equivalent macOS work in ARCA stops at scaffold/API parity on Windows:
no macOS compiling, no MoltenVK linking, and no VideoToolbox proof from this
machine. VideoToolbox through the render API remains a later mpv-fork gate
because the hardware decode path needs an `ra_ctx`/`ra_create_pl` bridge and
functional sign-off on a real Mac.

## Manual Visual Checks

- Sidebar switches between Home, Browse, Library, Search, Player, Queue,
  Settings.
- Browse filter chips switch rows and selecting a card opens Player.
- Library rows select a root and folders drill in/out.
- Search returns stub results and selecting a result opens Player.
- Player remains a placeholder until the MoltenVK/Vulkan render surface is
  wired.
