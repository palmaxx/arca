# ARCA — native 4K HDR / Dolby Vision media player

Cross-platform (Windows now, macOS next) player built as a **C++ core with a
flat C ABI** (engine control, render data plane, library DB) under **thin
per-platform UI shells** (WinUI3 today; SwiftUI later). Video renders through
a custom libmpv fork exposing mpv's gpu-next (libplacebo) renderer over the
render API with real HDR (`pl-d3d11` + `TARGET_COLORSPACE`).

Project docs: [PLAN.md](PLAN.md) (roadmap/status) ·
[DECISIONS.md](DECISIONS.md) (ADRs) ·
[docs/verification/day0.md](docs/verification/day0.md) (verification record).

## Layout

```
core/                C++20 core -> arca_core.dll (public ABI: core/include/arca)
  src/engine/        libmpv lifecycle + D3D11 HDR render sessions
  src/library/       SQLite library DB; offline/online hard seam
shells/windows/Arca  WinUI3 shell (unpackaged, x64) — P/Invokes the C ABI
shells/macos/ArcaMac SwiftUI shell scaffold — stub core by default
tools/hdr-verify     playback/HDR/DV/seek verification host (CLI)
tools/lib-verify     library ABI test gate
tools/db-seed        scripted library imports
third_party/mpv      vendored fork artifacts (refresh.ps1 + PROVENANCE.md)
third_party/sqlite   vendored SQLite amalgamation
```

## Build (Windows)

Prereqs: VS 2026 (MSVC x64), CMake+Ninja on PATH, .NET 8 SDK, and the mpv
fork built at `C:\DEV\ai-dev\projects\mpv-wt-hdr` (MSYS2 UCRT64).

```powershell
# 1. Vendor/refresh libmpv artifacts (fork DLL + MSYS2 runtime closure + import lib)
.\third_party\mpv\refresh.ps1

# 2. Native core + tools  ->  build\win-x64-release\bin
.\build.ps1

# 3. Gates
.\build\win-x64-release\bin\lib-verify.exe
.\build\win-x64-release\bin\hdr-verify.exe <clip> --seconds 10 --seek-test

# 4. WinUI3 shell
dotnet build shells\windows\Arca\Arca.csproj -p:Platform=x64
.\shells\windows\Arca\bin\x64\Debug\net8.0-windows10.0.19041.0\win-x64\Arca.exe [file]
```

## Day 0 state

4K HDR10 + Dolby Vision profile 8.1 playback verified end-to-end (software
decode per ADR-003; RPU passthrough confirmed with real and synthesized
content), library import/browse/delete with the offline/online separation,
keyboard transport + scrubbing, fullscreen. See the verification record for
measured numbers and the remaining user-run checks.

## Current sprint state

Post-Day-0 work is moving Fluss-style browse/library/search/progress/queue
behavior into the C++ core while reshaping the Windows shell around the Fluss
layout. The macOS shell is scaffolded as a SwiftPM SwiftUI app; it runs against
stub data until a Mac can build and link the native core. The macOS render path
is Vulkan over MoltenVK, not native Metal.

Reference docs:
[docs/architecture/core-abi.md](docs/architecture/core-abi.md),
[docs/reference/fluss-s1-map.md](docs/reference/fluss-s1-map.md),
[docs/macos/bringup.md](docs/macos/bringup.md),
[docs/macos/day0-equivalent.md](docs/macos/day0-equivalent.md).
