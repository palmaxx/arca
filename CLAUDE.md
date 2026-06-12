# CLAUDE.md

Guidance for Claude Code sessions in this repo.

## What this is

Native 4K HDR/DV player: C++20 core (`arca_core.dll`, flat C ABI in
`core/include/arca/`) + thin WinUI3 shell. Video path = vendored libmpv fork
(gpu-next over the render API, `pl-d3d11`, `TARGET_COLORSPACE`). Read
[PLAN.md](PLAN.md) for status, [DECISIONS.md](DECISIONS.md) for ADRs before
changing architecture-level behavior.

## Build & verify

```powershell
.\build.ps1                                   # native core + tools (MSVC+Ninja via vcvars)
.\build\win-x64-release\bin\lib-verify.exe    # library ABI gate (must PASS)
.\build\win-x64-release\bin\hdr-verify.exe <clip> --seconds 10 --seek-test
dotnet build shells\windows\Arca\Arca.csproj -p:Platform=x64   # shell
.\third_party\mpv\refresh.ps1                 # re-vendor after rebuilding the mpv fork
```

Interactive pass: `docs/verification/smoke-checklist.md`.
Test content: `testdata/local/` (gitignored; DV8.1 synthetics — regenerate
with dovi_tool + MSYS2 ffmpeg per `docs/verification/day0.md`).

## Load-bearing constraints (violating these breaks verified behavior)

- **Render sessions wrap a core-owned intermediate texture, never the
  swapchain backbuffer** (ADR-004: engine holds wraps across frames →
  `ResizeBuffers` would fail).
- **The engine runs `--border-background=none`; the core clears its own
  target** (wrapped R10G10B10A2 lacks `blit_dst` on NVIDIA).
- **Tone-map target = display peak from `IDXGIOutput6`, located by HMONITOR
  across all adapters** — not `GetContainingOutput` (fails cross-adapter on
  hybrid GPUs), not content mastering peak.
- **Device = high-performance adapter** (iGPU default cannot tone-map 4K60).
- **`hwdec=no` is deliberate** (ADR-003): render-API hwdec isn't in the fork
  yet; CPU decode is measured fine. Don't "fix" by enabling hwdec.
- **Offline libraries must never gain network access**; online enrichment is
  a pending queue (`online_media_info`) — no fetcher exists yet by design.
- Shells stay presentation-only; anything stateful belongs in the core.

## Conventions

- Core: C++20, /W4, dependencies PRIVATE (nothing leaks through the ABI);
  list payloads cross the ABI as JSON; strings are UTF-8, caller-freed via
  `arca_string_free`.
- Engine event callbacks fire on internal threads; shells marshal
  (`PlayerEngine` does this — keep that pattern).
- Update PLAN.md status and append ADRs to DECISIONS.md as part of the
  change, not after.
- Small, reviewable commits.
