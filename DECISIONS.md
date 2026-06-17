# DECISIONS.md — ADR log

> One entry per significant decision: context, options weighed, tradeoffs,
> outcome. Newest at the bottom. Companion: [PLAN.md](PLAN.md).

---

## ADR-000 — Reference-repo ground truth corrections (Phase A)

**Date:** 2026-06-11 · **Status:** Recorded (informational)

**Context:** Phase A discovery found the build brief's picture of the
reference repos outdated in ways that change the decision space.

**Findings (full detail in PLAN.md Phase A):**
1. The mpv render-API fork lives at `C:\DEV\ai-dev\projects\mpv-src`
   (active worktree `C:\DEV\ai-dev\projects\mpv-wt-hdr`), not
   `C:\DEV\ai-dev\mpv-src`. Its D3D11 + Vulkan HDR render-API backends are
   **done and validated on real HDR hardware**, not in-progress. macOS is
   covered today via MoltenVK over the `pl-vulkan` backend; native Metal is
   not the current path because libplacebo has no Metal `pl_gpu`. Render-API
   hwdec is GL-only → the HDR paths software-decode for now.
2. Fluss contains a substantial, portable .NET 8 core (library indexing,
   Local/Online separated metadata pipelines, TMDB/TVDB, probing, thumbnails,
   FTS5 search, queue, watch history) — directly relevant to §3 Option B.
3. The libVLC 4.0 alternative is unreleased and HDR-inferior on D3D11
   (VLC's own pipeline, not libplacebo) — confirmed non-viable.

**Consequence:** The §3 core-language ADR (next) must be argued against this
corrected baseline, not the brief's.

---

## ADR-001 — Shared core language: C++ (brief §3, Option A)

**Date:** 2026-06-11 · **Status:** Accepted (user criterion: quality / clean
optimized stack first; C# only if it saves work *without* making the stack a
mess)

**Decision:** The shared core (engine control, DB, library, probing seams,
indexing, grouping) is **C++20, exposing a flat C ABI** consumed by both
shells. Windows shell = WinUI3/C# (Fluss-derived) calling the C ABI via
P/Invoke for the *control plane only*. macOS shell = SwiftUI importing the
same C ABI natively. The render data plane (mpv render context, per-frame
texture handoff, colorspace negotiation) lives **entirely inside the native
core** on both platforms — shells hand in a surface and never touch frames.

**Why C# (Option B) was rejected — the honest evaluation:**

1. **macOS delivery is an actual limitation, not boilerplate.** The clean way
   to expose .NET to Swift is NativeAOT with `[UnmanagedCallersOnly]` C
   exports — but **EF Core is not production-supported under NativeAOT**
   (experimental in EF 9, compiled-models-only, real feature holes), and
   Fluss's `LibraryService` (~95 KB, the single biggest reuse item) creates
   `FlussDbContext` + EF LINQ throughout. So either:
   - NativeAOT → the main savings item must be rewritten anyway (raw SQLite
     in C#), or
   - host the full CoreCLR runtime inside the SwiftUI app bundle (~70 MB+,
     GC threads, runtime startup) — the footprint/cleanliness regression this
     project exists to escape.
2. **The render path would be implemented twice or marshaled twice.** With a
   C# core, per-frame render either crosses Swift→.NET→libmpv with
   `VkImage`s + semaphores marshaled through two boundaries, or each shell
   does render integration natively in its own language — duplicating the
   single riskiest code in the project. The C++ core implements it **once, in
   the language of the already-validated harnesses** (`rapi_hdr_present.c`,
   `rapi_harness_{d3d11,vulkan}.c` port nearly verbatim).
3. **Interop surface count favors C++.** C# core = hand-written libmpv
   P/Invoke binding (incl. render API + native callbacks) *plus* a
   Swift↔.NET bridge. C++ core = one C ABI, which Swift imports natively
   (zero-cost) and C# P/Invokes (first-class, thin, control-plane-only).
4. **On the "weaker/slower" question asked:** per-call latency of .NET C
   exports is *not* meaningfully slower — the rejection is architectural
   (runtime hosting, AOT limits, callback/GC pinning lifetime rules, doubled
   render boundary), not microbenchmarks.
5. Brief §3 tiebreaker — "prefer the path that keeps the mpv render
   integration lowest-friction" — independently selects C++.

**What survives from Fluss under this decision:**
- The **WinUI3 shell** (XAML views, command/keyboard services, navigation) as
  the Windows shell base — shells are presentation-only, so C# is the right
  tool there; it P/Invokes the core's control plane.
- The C# library/metadata logic is demoted to **reference spec**: the C++
  implementation ports its behavior (Local/Online pipeline separation,
  content-derived media IDs, `ResetCatalogFields` self-healing, retry
  design from Fluss's scan-recovery plan), with streamxs's unit-tested
  filename-parser/classifier as behavioral fixtures.

**Costs accepted:** rewriting library/indexing/metadata logic in C++ (SQLite
direct + FTS5, HTTP/JSON deps via vcpkg for the later online phase); slower
Day 0 library milestone than direct C# reuse. Mitigation: port
file-by-file against the two reference implementations rather than designing
from scratch.

---

## ADR-002 — libmpv consumption: vendored prebuilt artifacts from the local fork

**Date:** 2026-06-11 · **Status:** Accepted

**Context:** The fork's canonical git history lives in WSL (`~/mpv-fork`)
with a Windows clone + build worktree (`mpv-wt-hdr`, MSYS2 UCRT64). It is not
on a hosted remote, so a git submodule would be fragile; building mpv inside
our CMake tree would drag MSYS2/meson into every build.

**Decision:** Vendor **prebuilt artifacts** into `third_party/mpv/`:
`include/mpv/*.h` (API 2.7 headers incl. `render_d3d11.h`/`render_vulkan.h`),
`libmpv-2.dll`, and an MSVC import library generated from it
(`gendef`/`lib.exe /def`), plus `PROVENANCE.md` (fork branch + commit + build
date) and a `refresh.ps1` that re-copies from `mpv-wt-hdr\bld` and regenerates
the import lib. libmpv's flat C ABI makes the MinGW-built DLL ↔ MSVC-host
combination supported practice (same shape as the public mpv-dev packages);
M0 smoke-tests it before anything is built on top.

**Tradeoffs:** binary vendoring needs provenance discipline (hence
`PROVENANCE.md` + script); in exchange, player contributors never need the
MSYS2/meson toolchain, and the mpv fork keeps its own validated build/gate
workflow untouched.

---

## ADR-003 — Day 0 runs software decode; hwdec deferred behind a verification gate

**Date:** 2026-06-11 · **Status:** Accepted (user-approved)

**Context:** Render-API hwdec is GL-only in the fork today; the `pl-d3d11`
HDR path software-decodes (deliberate mpv-side deferral; needs the host's
decode device threaded through).

**Decision:** Day 0 ships on software decode. The deferral is **contingent on
milestone M2's verification gate passing** (user-run, on the HDR display):
- HDR10: static metadata + display-peak negotiation
  (`IDXGIOutput6` MaxLuminance → `TARGET_COLORSPACE`), `video-out-params`
  identity vs windowed mpv.
- **Dolby Vision: RPU passthrough verified** on profile 5 and profile 8
  samples — libplacebo reshaping active (dovi flagged in playback properties
  + on-display visual check vs windowed mpv).
- SW-decode headroom measured on 4K24 and 4K60 high-bitrate HEVC samples
  (dropped-frame stats, CPU load).
- Seek/scrub robustness under SW decode.

**If the gate fails** (DV broken on the render path, or 4K60 unwatchable):
pull the mpv-side render-API hwdec phase forward before Day 0 sign-off
(option (c) from PLAN.md §6) instead of shipping the deferral.

---

## ADR-004 — Render into an intermediate texture, not the swapchain backbuffer

**Date:** 2026-06-12 · **Status:** **RETIRED 2026-06-13** — superseded by the
engine-side fix (see "Upstream status" below). The core now wraps the
swapchain backbuffer directly (`GetBuffer → clear → render → Present`);
border clearing moved to a transient backbuffer RTV. Re-validated: resize
mid-playback applies cleanly, zero steady-state drops on 4K24/4K60, seek
test PASS, shell panel path green. **Floor requirement:** the vendored
engine must be at or past fork commit `008434c` (P5b.1) — older DLLs
reintroduce the `ResizeBuffers` failure. Historical rationale kept below.

**Context:** The fork's reference host wraps the swapchain backbuffer
directly (`GetBuffer → render → Present`) and assumes no backbuffer
reference survives between frames. M0's automated resize test disproved
that: the engine keeps its `pl_tex` wrap of the last-rendered texture alive
until the *next* render call, so `ResizeBuffers` fails with
`DXGI_ERROR_INVALID_CALL` whenever the wrapped texture is a backbuffer.

**Decision:** The core renders into a core-owned `R10G10B10A2`
RT+UAV intermediate texture and `CopyResource`s to the backbuffer before
`Present`. One trivial GPU copy per frame buys an always-legal
`ResizeBuffers` (constant occurrence in a real shell: window drags,
SwapChainPanel relayouts) and keeps engine-held references off swapchain
resources entirely. HDR negotiation, `TARGET_COLORSPACE`, and the
display-peak rule are unchanged from the validated sequence.

**Also fixed under this ADR:** destroying a render session (or engine) now
stops playback first — freeing the render context mid-file otherwise
surfaces a spurious "video output initialization failed" engine error.

**Upstream status (2026-06-13):** the root cause was an engine-side defect
(the fork's d3d11 backend held its host-texture wrap across render calls).
The source fix **landed and gated**: fork commits `008434c` (P5b.1,
caller-owned wraps released by end of render, incl. the error-clear gate)
+ `dc7b021` (public lifetime-contract docs); spec/record:
`C:\DEV\ai-dev\projects\mpv-src\_refactor\plan2-hdr-render-api\hdr-phase5b-assessment.md`.
ARCA re-vendored at `dc7b021` and retired the indirection the same day.
Note the `--border-background=none` + core-side clear remains — that is
the separate libplacebo `blit_dst` limitation, unchanged by P5b.1.

---

## ADR-005 — Shell-facing library, search, progress, and queue logic lives in the C++ core

**Date:** 2026-06-13 · **Status:** Accepted.

**Context:** The Fluss WinUI3 player has useful C# logic for child browsing,
FTS5 search, watch progress, and playback queues. ARCA needs equivalent
behavior in both WinUI3 and SwiftUI without duplicating stateful logic in two
native shells.

**Decision:** Port the shell-facing read models and state transitions into the
C++ core ABI:

- `arca_library_children_json`
- `arca_media_search_json`
- `arca_progress_*`
- `arca_queue_*`

The shells receive denormalized JSON for presentation and invoke commands
through the ABI. Windows wraps these calls in `LibraryStore`/`PlaybackQueue`;
macOS uses the same model through `ArcaCoreClient`.

**Consequences:** Shell UI code can be richer without becoming the source of
truth. Future metadata, tags, persistent queue/history, and settings should
extend this ABI rather than reintroducing platform-specific business logic.
