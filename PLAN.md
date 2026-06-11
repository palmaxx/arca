# PLAN.md — Cross-Platform 4K HDR/DV Player ("ARCA")

> Living roadmap. Companion: [DECISIONS.md](DECISIONS.md) (ADR log).
> Build brief: [REQUIREMENTS.md](REQUIREMENTS.md).

## Status

| Phase | State | Date |
|---|---|---|
| **A — Discovery** | ✅ Approved (with direction: quality-first §3 criterion; hwdec deferral accepted contingent on DV/RPU verification) | 2026-06-11 |
| **B — Plan & scaffold** | ✅ Drafted — ADR-001/002/003 resolved, layout + milestones below. **Awaiting checkpoint approval.** | 2026-06-11 |
| C — Day 0 build | Not started (blocked on B approval) | |

---

# Phase A — Discovery findings

## 1. mpv render-API fork — `C:\DEV\ai-dev\projects\mpv-src`

> ⚠️ The brief's path `C:\DEV\ai-dev\mpv-src` is stale. Real locations:
> main Windows clone `C:\DEV\ai-dev\projects\mpv-src`, **active HDR worktree
> `C:\DEV\ai-dev\projects\mpv-wt-hdr`** (branch `gpu-next-render-api-hdr`),
> canonical git history + regression oracle in WSL `~/mpv-fork`.

**The brief treats this as an in-progress "ambitious linchpin". It is in fact
substantially DONE and hardware-validated** (state as of 2026-06-04):

- **Plan 1 — gpu-next through the libmpv render API (GL): DONE**, including
  hwdec (F-phase), merge-audit fixes, and native-resource forwarding.
  `vo_gpu_next.c`'s libplacebo core was extracted into a shared
  `video/out/gpu_next/core.{c,h}` consumed by both the windowed VO and the new
  `render_backend_gpu_next`. Bit-identical windowed no-regression at every
  commit (lavapipe 12-case golden).
- **Plan 2 — HDR over the render API: core DONE.**
  - `MPV_RENDER_API_TYPE_PL_D3D11` implemented + validated: WARP self-baseline,
    genuine 10-bit PQ, and **confirmed end-to-end on the real NVIDIA RTX 4060
    HDR display** — render-API HDR target negotiation bit-identical to the
    windowed swapchain, on-display visual match (Phase 4b/5a GREEN).
  - `MPV_RENDER_API_TYPE_PL_VULKAN` implemented + validated (lavapipe
    byte-stable; HDR also proven on the RTX 4060 via `rapi_harness_vulkan`).
  - `MPV_RENDER_PARAM_TARGET_COLORSPACE` public param wired (mpv-side enums,
    ABI-safe, API version 2.7).
  - **macOS strategy: MoltenVK via the existing `pl-vulkan` backend — no new
    mpv code needed.** Native Metal is deferred (libplacebo's `metal.h` ships
    only on macOS; must be authored on a Mac).
- **Build artifacts exist**: `mpv-wt-hdr\bld\libmpv-2.dll` + `mpv.exe`
  (built 2026-06-04, MSYS2 UCRT64: `ninja -C bld` with `MSYSTEM=UCRT64`).
- **Reference host for our player** (= mpv Plan 2 "Phase 5b production shell"):
  `mpv-wt-hdr\_golden\render_api\rapi_hdr_present.c` — bare-Win32 HDR
  swapchain host, visually matched against windowed mpv on the HDR display.
  Proven call sequence: `GetBuffer → mpv_render_context_render(D3D11_TEX +
  TARGET_COLORSPACE) → Present`.

**Load-bearing host requirements discovered by that work** (must be honored in
our shell):
1. The host **must query the display peak**
   (`IDXGIOutput6::GetDesc1().MaxLuminance`) and pass it in
   `TARGET_COLORSPACE` — passing content mastering peak instead over-saturates.
2. HDR screenshots (Snipping Tool / OS SDR grab) are **not** a valid
   verification method; verify via `video-out-params` identity + on-display
   visual comparison (matters for §9's "verify HDR for real" guardrail).
3. D3D11 host texture: `USAGE_DEFAULT`, single-sample, single-mip,
   `BIND_RENDER_TARGET`.

**Known limitations that directly shape Day 0:**
- **Render-API hwdec is GL-only. The D3D11 and Vulkan render-API backends are
  software-decode only** (deliberately deferred mpv-side phase; needs the
  host's decode device threaded through + a real-GPU harness). Day 0 4K HDR
  playback through `pl-d3d11` will therefore **software-decode** (HW decode
  via d3d11va works on the *windowed/`--wid`* path, which we're moving away
  from). Risk + mitigations tracked below.
- Render-API screenshot output differs from windowed (native BT.2020/PQ vs
  tone-mapped sRGB) — intentional, orthogonal to display rendering.

Validation infrastructure (per-developer, git-excluded): WSL lavapipe golden
gates, WARP D3D11 + lavapipe Vulkan harnesses, `run-hdrval.ps1` real-HDR gate.
Full audit map: `mpv-src\_refactor\MERGE_AUDIT_HANDOFF.md`.

## 2. Fluss — `C:\DEV\VS2026\Fluss` (WinUI3, .NET 8)

**The brief undersells Fluss** ("less mature logic"). It is a working WinUI3
player with a substantial, largely **UI-independent C# core** — much of the
§7 Day 0 scope already exists here in some form:

- **Data layer**: EF Core 8 + SQLite (`FlussDbContext`; content-derived media
  IDs); FTS5 search (raw SQL); watch history; tags; queue with persisted
  snapshot.
- **Library pipeline** (`LibraryService`, ~95 KB): folder indexing,
  `FileSystemWatcher` re-index, recovery/retry planning underway (root
  `PLAN.md` there is a fresh scan-recovery design — active development).
- **The OFFLINE/ONLINE hard separation §7 demands already exists** as
  `MetadataMode`, "fully separated pipelines":
  - **Local** = pure file browser; ffprobe + generated previews only;
    `ResetCatalogFields` actively strips catalog metadata; self-heals polluted
    items. Never queries online.
  - **Online** = filename parse (`FilenameMediaParser`) → TMDB/TVDB providers
    → poster/backdrop → grouped movies/series (`OnlineMediaGroups`).
- **Media tools**: bundled ffprobe/ffmpeg, `MediaToolLocator`, thumbnail +
  artwork caching (with a hard-won MSIX storage-path gotcha documented in its
  CLAUDE.md).
- **Playback**: `IPlaybackEngine` abstraction with
  `MediaFoundationPlaybackEngine`; **an mpv engine is scaffolded but not
  implemented** (`PlaybackEngineRouter` always returns MediaFoundation).
  Confirms the brief: current playback depends on MS Store codecs + MF-tier
  HDR.

**Portability audit (drives the §3 decision):**
- WinUI/WinRT coupling in `Services\` is confined to: `PlaybackService` +
  `PlaybackEngine\*` (expected — they wrap WinRT `MediaPlayer`; the
  `IPlaybackEngine` interface itself leaks `MediaPlayer` and `StorageFile`,
  so it needs reshaping for any mpv engine regardless of language) and
  `KeyboardMapService` (UI concern). One WinRT line in `AppStoragePaths`.
- **Everything else — library, metadata, search, queue, history, tags,
  browse, probing, thumbnails (~200 KB of C#) — is portable .NET 8** (EF
  Core/SQLite/HttpClient/Process all run on macOS).
- The shell is a monolithic `MainWindow.xaml.cs` (~2400 lines, no DI,
  manual service construction) — functional "basically ready" Windows shell
  per the brief, but the wiring would need refactoring to sit on a shared
  core regardless of Option A/B.
- `Fluss.Tests` is empty scaffolding. Build claimed via
  `dotnet build Fluss/Fluss.csproj -p:Platform=x64` — **not yet verified by
  me** (Phase B verification item).

## 3. Streamxs — `C:\DEV\ai-dev\projects\streamxs` (Electron + Rust/NAPI + mpv)

Internal name `infuse-windows` / "Stash for Windows". Confirms the brief:
most feature-complete, architecturally what we're leaving (Electron, 4
BrowserWindows, mpv embedded as child HWND via `--wid` + DirectComposition,
Rust NAPI addon for mpv/WASAPI/window chrome).

**UX/feature reference inventory** (the "take" from this repo):
- **Pages**: Home, Browse, Library, MediaDetail, Player, Profiles, Search,
  Settings, Tags, Downloads, AudioSetupWizard.
- **Indexer pipeline** (`packages/main/src/indexer/`): filename-parser →
  content-classifier (decides TMDB vs TVDB) → metadata fetch → thumbnail
  queue (incl. thumbnail-seek logic) → chokidar file-watcher for live updates.
  Heavily unit-tested (parsers, classifier, budget, progress are `*.test.ts`).
- **Settings model**: per-library, per-profile, server, video settings as
  separate tested modules — the brief's "per-folder/per-user settings".
- **Multi-window player UX**: separate overlay window for player controls,
  PiP window + PiP controls window.
- **Audio**: WASAPI exclusive mode path.
- **DB**: SQLite WAL, numbered SQL migrations, all queries centralized,
  FTS5 search.
- Typed end-to-end IPC (Zod schemas) — Electron-specific, not portable, but
  the channel list is a de-facto feature catalog.
- Manual playback smoke checklist in its CLAUDE.md worth porting to our
  verification docs.

## 4. players — `C:\DEV\ai-dev\projects\players` (brainstorm; low priority, mined)

- Scaffold-only monorepo for an **FFmpeg + standalone-libplacebo C++ engine**
  (`engine/` static lib with public `engine.h`/`output_sink.h`/`hdr.h`,
  `player/` SDL3+ImGui shell, dormant Rust `hwframe-wgpu` bridge). Engine
  factory returns `nullptr`; no pipeline was ever built. **Superseded** by the
  mpv-fork path, but its *build-enforced library boundary* rationale
  (`docs/REPO_LAYOUT.md`) is directly reusable for our monorepo design.
- **`insights.md` = verified-truths compendium (2026-05-17/18)** — the sanity
  check the brief mandates. Key findings still relevant:
  - **libVLC 4.0**: the brief's note checks out as *unreleased* (~2 yr late;
    nightly-only; `libvlc_video_set_output_callbacks` is 4.0-only +
    experimental). And quality-wise it does **not** match our path: VLC's
    D3D11 HDR is VLC's own pipeline, *not* libplacebo-tier. → mpv fork remains
    the right call.
  - HDR quality tiers: libplacebo/gpu-next (SOTA) > VLC-D3D11 ≈ GStreamer >
    stock libmpv render API. Web stacks (Electron/Tauri) cannot composite
    HDR video today at all — re-validates the §1 non-negotiable.
  - Note: insights.md *predates* the fork's completion (it concluded gpu-next
    embedding needed unmerged upstream draft PR #16818 — our fork has since
    implemented exactly that seam properly, with HDR, which upstream still
    lacks).
- "Plan 4" (per-platform thin shells + shared engine) context actually lives
  in **[player-architecture-options.md](player-architecture-options.md)** in
  this repo (Option 4) — a thorough 6-option comparison already written,
  feeding Phase B directly.

## 5. Brief assumptions: confirmed vs contradicted

| Brief assumption | Verdict |
|---|---|
| mpv work lives at `C:\DEV\ai-dev\mpv-src` | ❌ Stale path — see §1 |
| Render API is in-progress, "debug while building the player" | ❌ (Good news) Core is done + HW-validated; remaining mpv-side items are render-API hwdec (D3D11/Vulkan), native Metal (Mac-only), upstream merge hygiene |
| d3d11/vulkan/metal backends | ⚠️ d3d11 + vulkan done; **metal deferred — macOS = MoltenVK over pl-vulkan** (works today, no new code) |
| Fluss "less mature logic" | ❌ Substantial portable C# core incl. the exact Local/Online separation §7 requires, TMDB/TVDB, probing, thumbnails, FTS5, queue, history |
| Fluss Windows shell "basically ready" | ✅ Functional, but monolithic (~2400-line MainWindow, no DI) — needs rewiring onto a shared core either way |
| Fluss depends on MS Store codecs | ✅ For its current MediaFoundation engine; mpv engine scaffolded, never implemented |
| Streamxs = best UX/feature reference, worst architecture | ✅ Confirmed in detail |
| players: "libvlc 4.0 reportedly does roughly the same" | ⚠️ Exists but unreleased **and** HDR-inferior on D3D11 (not libplacebo) — not a viable alternative |

## 6. Key risks carried into Phase B

1. **Day 0 4K HDR = software decode** on the `pl-d3d11` render path (render-API
   hwdec is GL-only today). On the user's rig (RTX 4060 + unknown CPU) 4K
   HEVC/DV SW decode may be fine at 24 fps and marginal at 4K60 high-bitrate.
   Options to weigh in Phase B: (a) accept SW decode for Day 0 + schedule the
   mpv-side hwdec phase, (b) keep a windowed `--wid` fallback mode purely as a
   stopgap, (c) pull the mpv hwdec phase forward before Day 0 sign-off.
2. **Dolby Vision specifics unverified** on the render-API path (HDR10/PQ is
   what's been validated; DV reshaping via libplacebo + RPU passthrough needs
   an explicit Day 0 verification case on the HDR display).
3. **C# ↔ libmpv P/Invoke surface** (if Option B): flat C API is
   P/Invoke-friendly, but the render API involves per-frame native callbacks +
   D3D11 texture pointers — needs a careful binding layer either way.
4. **macOS C# story** (if Option B): the core itself runs on .NET 8/macOS, but
   SwiftUI ↔ .NET hosting/interop is real friction (NativeAOT C exports or
   runtime hosting). Option A has the mirror-image cost (C++ core ↔ WinUI3
   interop on Windows).
5. Fluss build not yet verified by me; mpv fork DLL exists but our consuming
   toolchain (MSYS2-built DLL ↔ MSVC/.NET host) needs a smoke test early in
   Phase C milestone 1.

---

# Phase B — Plan & scaffold

## Architecture (resolved)

- **ADR-001: C++20 shared core exposing a flat C ABI**; WinUI3/C# shell
  (Fluss-derived) on Windows via control-plane P/Invoke; SwiftUI on macOS
  importing the same C ABI. Render data plane fully inside the core (shells
  hand in a surface, never touch frames). Fluss C# library logic becomes the
  reference spec for the C++ port; streamxs parser tests become behavioral
  fixtures.
- **ADR-002: libmpv vendored as prebuilt artifacts** (headers + DLL + MSVC
  import lib) from `mpv-wt-hdr`, with provenance + refresh script.
- **ADR-003: Day 0 = software decode**, contingent on the M2 verification
  gate (incl. Dolby Vision RPU passthrough). Gate failure pulls the mpv-side
  render-API hwdec phase forward.

## Monorepo layout

```
ARCA/
  CMakeLists.txt, CMakePresets.json, vcpkg.json   # core + tools build (MSVC x64)
  core/
    include/arca/        # public C ABI: arca.h, arca_engine.h, arca_render.h,
                         #   arca_library.h, arca_events.h
    src/
      engine/            # libmpv lifecycle, command/property/event loop,
                         #   render sessions (pl-d3d11 now, pl-vulkan later),
                         #   TARGET_COLORSPACE + display-peak negotiation
      library/           # scan/index/group; offline|online pipeline seam
      db/                # SQLite direct (+FTS5): schema, migrations, queries
      media/             # probing/thumbnail seams only (post-Day-0 impl)
      util/
    tests/               # core unit tests (parser fixtures from streamxs)
  shells/
    windows/Arca/        # WinUI3 C# app (Fluss-derived shell) + Interop/ (P/Invoke)
    macos/               # SwiftUI shell — later phase (MoltenVK / pl-vulkan)
  third_party/mpv/       # vendored: include/, libmpv-2.dll, mpv.lib,
                         #   PROVENANCE.md, refresh.ps1   (ADR-002)
  tools/hdr-verify/      # standalone HDR smoke host (rapi_hdr_present port)
  assets/                # shared assets; ffprobe/ffmpeg land here post-Day-0
  docs/verification/     # M2 gate results, playback smoke checklist
```

Boundary rule (from players' `REPO_LAYOUT.md`, kept): shells may include only
`core/include/arca/`; core deps link PRIVATE and never leak through the ABI.

## Day 0 milestones (Phase C) — each must build & run before the next

- **M0 — Toolchain + first light.** Scaffold repo/CMake/vcpkg; vendor libmpv
  artifacts + generate MSVC import lib; `arca_core.dll` skeleton (engine
  create/load/play + pl-d3d11 render session through the C ABI);
  `tools/hdr-verify` bare-Win32 host plays an HDR clip through the core.
  *Gate:* MinGW-DLL↔MSVC link proven; `video-out-params` matches windowed mpv
  (PLAN §1 risk 5 retired).
- **M1 — WinUI3 shell embed.** Fluss-derived shell hosts the core via
  `SwapChainPanel` (panel native ptr → core; core owns swapchain + render
  thread + present). Keyboard controls, scrubbing, File→Open single file.
  *Gate:* 4K HDR plays in-shell with controls; HDR verified on-display, not
  via screenshots (PLAN §1 host requirement 2).
- **M2 — Playback verification gate (user-run, ADR-003).** HDR10 metadata +
  peak negotiation; **DV profile 5 + 8 RPU passthrough**; SW-decode headroom
  on 4K24/4K60 HEVC; seek robustness. Results →
  `docs/verification/day0.md`. *Gate failure ⇒ ADR-003 fallback (pull hwdec
  forward) before continuing.*
- **M3 — DB + library management.** SQLite schema v1 (libraries, media items
  w/ content-derived IDs, mode field per Fluss `MetadataMode`); import /
  view / delete a library folder; play single files from the folder view.
- **M4 — Offline/Online hard seam (scaffold).** Two architecturally distinct
  paths per brief §7: offline = explorer-like view whose code path links no
  network facility at all (enforced at the module boundary); online = items
  indexed/grouped by kind with a stubbed enrichment queue (no fetching yet).
  Probing, queue, media-detail remain seams only.
- **M5 — Day 0 wrap.** Keyboard map + menu polish, status surfacing,
  playback smoke checklist (ported from streamxs) run end-to-end; PLAN/
  DECISIONS updated; Day 0 sign-off against brief §7 incl. the §9 "verify
  HDR for real" guardrail.

Out of scope, seams only (brief §7): playback queue, online fetching,
probing, media detail. macOS shell + pl-vulkan/MoltenVK session and the
mpv-side render-API hwdec phase are the first post-Day-0 workstreams.

# Phase C — Day 0 build (pending B approval)
