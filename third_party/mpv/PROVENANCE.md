# Vendored libmpv provenance

- Source fork worktree: `C:\DEV\ai-dev\projects\mpv-src\_upload\mpv`
- Branch: `gpu-next-render-api-hdr`
- Commit: `a8c7cfe7812e3d828eb603397860500a1569c512`
- Built: MSYS2 UCRT64 (`ninja -C bld`); DLL timestamp 2026-06-19 16:51
- Vendored: 2026-06-19 17:04 by refresh.ps1
- Exports in mpv.lib: 54
- MSYS2 UCRT64 runtime closure: 110 DLLs (load-time deps via ldd;
  includes libdovi = Dolby Vision RPU support in libplacebo)

The fork adds the gpu-next libmpv render backend (`pl-d3d11` / `pl-vulkan` /
`pl-opengl`) and `MPV_RENDER_PARAM_TARGET_COLORSPACE` (client API 2.7).
D3D11VA render-API hwdec is implemented and validated. Vulkan exposes a
generic libplacebo RA for compatible interops; VideoToolbox-over-MoltenVK is
not yet validated and true Vulkan Video decoding is deferred.

Frozen source clone: `C:\DEV\ai-dev\projects\mpv-src\_upload\mpv`. Final audit:
`C:\DEV\ai-dev\projects\mpv-src\_refactor\FINAL_PR_AUDIT.md`.
