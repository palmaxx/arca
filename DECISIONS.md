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
   deferred (requires a Mac). Render-API hwdec is GL-only → the HDR paths
   software-decode for now.
2. Fluss contains a substantial, portable .NET 8 core (library indexing,
   Local/Online separated metadata pipelines, TMDB/TVDB, probing, thumbnails,
   FTS5 search, queue, watch history) — directly relevant to §3 Option B.
3. The libVLC 4.0 alternative is unreleased and HDR-inferior on D3D11
   (VLC's own pipeline, not libplacebo) — confirmed non-viable.

**Consequence:** The §3 core-language ADR (next) must be argued against this
corrected baseline, not the brief's.

---

*(ADR-001 — shared-core language [§3 Option A vs B]: pending, Phase B.)*
