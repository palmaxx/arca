# Vendored libmpv provenance

- Source fork worktree: `C:\DEV\ai-dev\projects\mpv-wt-hdr`
- Branch: `gpu-next-render-api-hdr`
- Commit: `dc7b021248c3d3b3d7e015f55eb27c5158c39027`
- Built: MSYS2 UCRT64 (`ninja -C bld`); DLL timestamp 2026-06-12 18:11
- Vendored: 2026-06-13 01:10 by refresh.ps1
- Exports in mpv.lib: 54
- MSYS2 UCRT64 runtime closure: 110 DLLs (load-time deps via ldd;
  includes libdovi = Dolby Vision RPU support in libplacebo)

The fork adds the gpu-next libmpv render backend (`pl-d3d11` / `pl-vulkan` /
`pl-opengl`) and `MPV_RENDER_PARAM_TARGET_COLORSPACE` (client API 2.7).
Canonical history: WSL `~/mpv-fork`; audit map:
`C:\DEV\ai-dev\projects\mpv-src\_refactor\MERGE_AUDIT_HANDOFF.md`.
