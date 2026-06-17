# macOS day-0-equivalent sprint

This sprint is scaffold/API parity only from the Windows machine. It does not
claim a macOS build, a MoltenVK render surface, or VideoToolbox hardware decode.

## Current Evidence

- `requirements.md` names `C:\DEV\ai-dev\mpv-src`; on this machine the checkout
  is at `C:\DEV\ai-dev\projects\mpv-src`.
- `C:\DEV\ai-dev\projects\mpv-mac-metal\METAL_MAC_START_HERE.md` supersedes the
  rejected native Metal plan. The usable path is `pl-vulkan` on MoltenVK.
- `mpv-mac-metal` is on branch `gpu-next-render-api-metal` with no status
  entries from `git status --short --branch`.
- `mpv-src` is `master...origin/master [behind 46]` with untracked `.claude/`;
  treat it as reference context, not ARCA's active render fork.
- mpv source confirms the macOS windowed path creates a Metal surface from the
  Vulkan backend (`video/out/vulkan/context_mac.m`), and VideoToolbox
  libplacebo interop expects a libplacebo RA (`video/out/hwdec/hwdec_vt_pl.m`).

## Sprint Goals

- Keep the macOS SwiftUI app opening into a native sidebar shell in stub mode.
- Add Browse to the same shared core contract used by Windows:
  `arca_media_browse_json`.
- Keep native core bridging ready behind `ARCA_NATIVE_CORE`, including Browse.
- Keep player UI honest: placeholder wording says Vulkan over MoltenVK, not
  native Metal.
- Document the later render sprint as MoltenVK/Vulkan surface integration, with
  VideoToolbox decode deferred until the mpv render API exposes the required
  RA context and a Mac can validate it.

## Validation I Can Run Here

- Native ARCA build succeeds on Windows.
- `lib-verify.exe` passes, including browse JSON filter coverage.
- WinUI shell builds after consuming the new browse ABI.
- macOS source contract is compile-oriented only: Swift files reference the new
  Browse model/client path consistently, but `swift run` remains a Mac-side
  validation step.

## End Criteria

- ARCA source has a macOS Browse screen reachable from the sidebar.
- Stub and native macOS clients expose the same browse method.
- Docs no longer describe native Metal as the next implementation path.
- Any remaining macOS work is explicitly a future Mac/MoltenVK validation item,
  not hidden inside the Windows day-0 sprint.
