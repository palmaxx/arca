# PLAN.md — Cross-Platform 4K HDR/DV Player ("ARCA")

> Living roadmap. Companion: [DECISIONS.md](DECISIONS.md) (ADR log).
> Build brief: [REQUIREMENTS.md](REQUIREMENTS.md).

## Status

| Phase | State | Date |
|---|---|---|
| **A — Discovery** | ✅ Approved (with direction: quality-first §3 criterion; hwdec deferral accepted contingent on DV/RPU verification) | 2026-06-11 |
| **B — Plan & scaffold** | ✅ Approved (user set the Day-0 implementation goal) | 2026-06-12 |
| **C — Day 0 build** | ✅ **All milestones done & gated** (M0, M1, M2a/M2b automated, M3, M4, M5). Two user-run checks remain in [docs/verification/day0.md](docs/verification/day0.md): DV profile-5 sample, on-display visual parity vs windowed mpv. | 2026-06-12 |
| **D — Fluss parity slice** | ✅ Implementation slice done: browse/library children/search/progress/queue moved into the C++ ABI; Windows shell reshaped toward Fluss with Browse and player overlays; macOS SwiftUI scaffold follows the same browse contract. User visual pass remains the acceptance check. | 2026-06-17 |

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
    mpv code needed for rendering.** Native Metal is not the current path:
    libplacebo has no Metal `pl_gpu`, so a true native Metal backend would be
    upstream libplacebo work first.
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
| Render API is in-progress, "debug while building the player" | ❌ (Good news) Core is done + HW-validated; remaining mpv-side items are render-API hwdec (D3D11/Vulkan/VideoToolbox), MoltenVK host integration, upstream merge hygiene |
| d3d11/vulkan/metal backends | ⚠️ d3d11 + vulkan done; **macOS = MoltenVK over pl-vulkan** for rendering (native Metal would require a libplacebo Metal backend first) |
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
    macos/ArcaMac/       # SwiftPM SwiftUI shell scaffold (stub core first)
  third_party/mpv/       # vendored: include/, libmpv-2.dll, mpv.lib,
                         #   PROVENANCE.md, refresh.ps1   (ADR-002)
  tools/hdr-verify/      # standalone HDR smoke host (rapi_hdr_present port)
  assets/                # shared assets; ffprobe/ffmpeg land here post-Day-0
  docs/verification/     # M2 gate results, playback smoke checklist
```

Boundary rule (from players' `REPO_LAYOUT.md`, kept): shells may include only
`core/include/arca/`; core deps link PRIVATE and never leak through the ABI.

## Day 0 milestones (Phase C) — each must build & run before the next

- **M0 — Toolchain + first light. ✅ DONE 2026-06-12.** Scaffolded repo/
  CMake (MSVC + Ninja via `build.ps1`); vendored libmpv + generated MSVC
  import lib + the 110-DLL MSYS2 runtime closure (incl. **libdovi** — DV RPU
  support present); `arca_core.dll` (engine + pl-d3d11 render session) +
  `tools/hdr-verify`. *Gate result:* 4K HDR10 HEVC plays through the core —
  `hdr_active=1`, BT.2020/PQ negotiated, display peak queried (271 nits),
  `video-out-params` identical to `video-params` (bt.2020/pq, sig-peak
  4.926108 — the fork's validated values), SW decode, 0 steady-state drops
  (~36-frame startup transient → M2). Resize + teardown hardened (ADR-004).
  MinGW-DLL↔MSVC link proven (§6 risk 5 retired).
- **M1 — WinUI3 shell embed. ✅ DONE 2026-06-12.** Fresh WinUI3 shell
  (unpackaged WinAppSDK 1.8) hosts the core via `SwapChainPanel`
  (composition swapchain created core-side, `SetSwapChain` on the UI
  thread, inverse composition-scale transform). Transport bar with live
  scrub, keyboard map (Space/←→/Shift/↑↓/M/F11), File→Open, CLI autoload.
  *Gate result:* DV8.1 file reaches Playing in-shell, `hdr_active=1`,
  271 nits, verified via window-title status.
- **M2 — Playback verification gate (ADR-003). ✅ automated part DONE
  2026-06-12** (pulled M2a forward, before the shell): **DV profile 8.1 RPU
  passthrough = GO** on real 75 Mbps content + dovi_tool-synthesized
  regression clips; SW-decode headroom = zero decoder drops everywhere
  incl. 4K60; **seek test PASS** (4 absolute seeks across 2h16m, exact
  landings, playback resumes); audio AC-3 5.1 via WASAPI. Hybrid-GPU
  fixes fell out of the data (high-perf adapter, HMONITOR-based peak
  query, core-side border clear). Remaining user-run: DV P5 sample,
  on-display visual parity. Full record:
  [docs/verification/day0.md](docs/verification/day0.md).
- **M3 — DB + library management. ✅ DONE 2026-06-12.** SQLite 3.51.3
  compiled in; schema v1 (libraries, content-derived media IDs,
  `online_media_info` queue). Import/browse/rescan/remove in the shell
  sidebar + `db-seed` CLI; double-click plays from the folder view.
  *Gate:* `lib-verify` PASS.
- **M4 — Offline/Online hard seam (scaffold). ✅ DONE 2026-06-12.**
  Offline = explorer-flat view, scan is pure filesystem enumeration; the
  core links no networking facility at all. Online = local filename
  parse → grouped-by-identity view (series under SxxEyy headers, movies
  by title+year) + `enrich_status='pending'` queue awaiting the future
  fetcher phase. Probing, playback queue, media-detail remain seams only.
- **M5 — Day 0 wrap. ✅ DONE 2026-06-12.** README, repo CLAUDE.md (build +
  load-bearing constraints), smoke checklist ported from streamxs
  ([docs/verification/smoke-checklist.md](docs/verification/smoke-checklist.md)),
  verification record finalized, PLAN/DECISIONS current.

## Post-Day-0 backlog (next workstreams, in rough order)

1. User-run M2 closure (DV P5 + on-display parity) — then ADR-003 stands.
2. mpv-side render-API **hwdec** phase (d3d11va through `pl-d3d11`) — planned
   in the fork at `_refactor/plan2-hdr-render-api/hdr-phase7-hwdec-plan.md`
   (D3D11 first; render device == decode device, so it mirrors the GL
   F-phase). Landing it retires ADR-003 here (flip the engine off `hwdec=no`).
3. Probing + thumbnails (ffprobe/ffmpeg vendoring; seams exist in
   `core/src/media/`).
4. Online metadata fetcher consuming the pending queue (TMDB/TVDB; port
   Fluss/streamxs provider behavior).
5. Media-detail page; settings persistence; persistent queue/history polish.
6. macOS shell: build SwiftUI scaffold on a Mac, then link the C ABI and start
   `pl-vulkan`/MoltenVK session work. VideoToolbox render-API hwdec remains a
   later mpv-fork gate and needs Mac functional validation.

Out of scope for the current prototype: online fetching, probing, media detail,
Mac render backend validation, and the mpv-side render-API hwdec phase.

## Phase D — Fluss parity slice

**Goal:** make the next prototype complex enough to exercise the shared-core
architecture: Home/Browse/Library/Search/Player/Queue/Settings in Windows,
plus a parallel SwiftUI shell scaffold that consumes the same model boundary.

**Done in this slice:**

- Core ABI owns Browse, immediate library children, FTS5 search,
  progress/resume, and an in-memory playback queue.
- Windows `LibraryStore`/`PlaybackQueue` P/Invoke those APIs.
- Windows shell adopts the Fluss-style title bar, navigation layout, home,
  browse, library explorer, search, queue, settings, and player overlays.
- macOS `ArcaMac` SwiftPM package provides SwiftUI models/views and a
  stub/native `ArcaCoreClient` split.
- Docs added for ABI contract, Fluss parity mapping, macOS bring-up,
  macOS day-0-equivalent scope, and manual smoke checks.

**Validation:** native build, `lib-verify`, Windows shell build. Visual
acceptance remains manual via `docs/verification/smoke-checklist.md`.

# Phase C — Day 0 build (pending B approval)
