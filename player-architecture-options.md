# Player Architecture Options — Reference & Comparison (2026-05)

> Captures the six host-shell architectures evaluated for the HDR media
> player. Decision-support / stack reference; execution sequencing
> deliberately omitted (per-option implementation lives in its own plan
> doc when committed to).
>
> Last updated 2026-05-23, after the W5-6 + W6 mpv fork refactor was
> signed off on commit `8a2df0e` (libplacebo render-API path
> reachable + real-D3D11-HDR bit-faithful through W6).

## Common substrate for the "new" options (1-4)

Options 1-4 all consume mpv via the **libmpv render API + new HDR
context-fns + `MPV_RENDER_PARAM_TARGET_COLORSPACE`** (planned in
[mpv-src/plan-hdr-render-api.md](c:\DEV\ai-dev\projects\mpv-src\plan-hdr-render-api.md)).
That plan is *host-agnostic* by design — the host provides a graphics
device + per-frame texture + colorspace info; mpv renders into it.

What this substrate gives every option below:
- libplacebo-grade HDR rendering (best-in-class tone mapping, gamut
  mapping, peak detect, custom shaders).
- Per-platform graphics backends: `MPV_RENDER_API_TYPE_PL_D3D11` for
  Windows, `_PL_VULKAN` for Linux/Wayland, `_PL_METAL` for macOS
  (D3D11 first; Vulkan/Metal are the cross-platform follow-ups).
- Same render hook, same `gpu_next_core`, same option pipeline as the
  windowed `--vo=gpu-next`. Bit-faithful to the windowed path on
  matching hardware.

Options 5 + 6 are *existing* architectures already in use; they do not
consume the new render API (they predate it).

---

## Option 1 — Qt 6.7+ + QQuickRhiItem + mpv render API

### Summary
Native cross-platform C++ shell. QtQuick (QML) for UI, Qt RHI for
graphics abstraction, `QQuickRhiItem` to host the mpv-rendered surface
inside the QtQuick scene. mpv writes into a RHI texture; Qt composites
it into the QtQuick scene + HDR swapchain.

### Stack
- Qt 6.7+ (Qt 6.6 first shipped RHI HDR; 6.7 is recommended baseline
  for stability).
- QtQuick + Qt Quick Controls 2 (UI runtime).
- Qt RHI (abstracts D3D11/Vulkan/Metal/OpenGL/OpenGL ES under one API).
- `QQuickRhiItem` (Qt 6.7+) — XAML-equivalent custom-render item.
- `QSurfaceFormat::setColorSpace()` + `QQuickWindow::setSwapChainFormat()`
  for HDR negotiation.
- libmpv + the new render API + the per-platform pl context-fns.

### Platforms / compatibility
- Windows, macOS, Linux (Wayland HDR available where compositor
  supports it — KDE Plasma 6.x best; GNOME limited).
- Qt 6.7 requires Win10 1809+, macOS 11+, recent glibc on Linux.
- License: LGPLv3 (dynamic link) or Qt Commercial (closed-source
  static link or hard-isolation requirements).

### HDR rendering
- Excellent. Qt RHI handles HDR swapchain negotiation per platform;
  mpv's render API provides libplacebo tone mapping into the
  RHI-managed HDR target.
- Cross-platform HDR is the strongest "for free" of any framework
  here.

### mpv integration mechanism
- Subclass `QQuickRhiItem`. In `initialize()`, take
  `QRhi::nativeHandles()` to get the native D3D11/Metal/Vulkan device.
- Hand that device to libmpv via the new render API on context
  create.
- In `render()`, get the RhiItem's `QRhiTexture` native handle for
  the current frame; pass to `mpv_render_context_render()` via
  `MPV_RENDER_PARAM_D3D11_TEX` (or Metal/Vulkan equivalent).
- Pass HDR target colorspace via `MPV_RENDER_PARAM_TARGET_COLORSPACE`.

### UI development model
- QML (declarative) + JavaScript for logic + C++ for backend code.
- QtQuick Controls 2 + custom styling for polish.
- Reactive bindings via QML property system.
- Mobile-like animations via QtQuick's animation framework.
- Qt Design Studio integration for designer handoff.

### Pros
- One codebase, three desktops (Windows + macOS + Linux).
- HDR rendering quality and HDR composition both first-class.
- Industry standard for cross-platform native desktop.
- Mature ecosystem (third-party QML controls, Qt Marketplace).
- Long-term Qt Company commercial backing (LTS releases, Qt 6.x
  branch active through ~2027+).
- QtQuick UI polish ceiling is achievable, just requires effort.

### Cons
- Multi-month migration cost if coming from a different shell.
- QML/JavaScript ecosystem smaller than React/web — fewer drop-in
  libraries for things like polished animation systems, data fetching
  patterns, state management.
- QtQuick "default look" is plain; matching Plex/Infuse polish
  requires design-system investment.
- Tied to QtQuick's scene-graph frame pacing (almost never matters
  for video playback, but worth knowing).
- License clarity around commercial use is a real consideration.

### Risks / known issues
- RHI HDR was new-ish (~2023); some platform combinations still have
  rough edges (especially Wayland HDR).
- `QQuickRhiItem` is Qt 6.7+; pinning to that minimum may exclude
  some Linux distros' shipped Qt.
- Native handle access via RHI is documented but the API surface for
  consuming D3D11 textures across RHI versions can shift.

### Existing prior art
- Plex Desktop (was Qt + custom Win32 integration, predates RHI).
- DroidCam, OBS Studio (Qt-based, use QtRHI for video).
- Various Qt-based media players post-Qt 6.7 (still emerging).

---

## Option 2 — WinUI 3 + SwapChainPanel + mpv render API

### Summary
Windows-only native shell. WinUI 3 (XAML) for UI, Microsoft.UI.
Composition (DComp-backed) for visual tree, `SwapChainPanel` to host
mpv-rendered swapchain. Equivalent role to QQuickRhiItem but in the
WinUI ecosystem.

### Stack
- WinUI 3 (Windows App SDK 1.x).
- XAML for UI.
- Microsoft.UI.Composition (wraps DComp under the hood).
- `SwapChainPanel` XAML element with `ISwapChainPanelNative::
  SetSwapChain()` for D3D11 swapchain hosting.
- DXGI swapchain configured for HDR
  (`DXGI_FORMAT_R10G10B10A2_UNORM` +
  `DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`).
- libmpv + new render API + `libmpv_pl_context_d3d11`.

### Platforms / compatibility
- Windows 10 1809+ / Windows 11 only.
- WinUI 3 has no macOS/Linux story.
- License: MIT (Windows App SDK).

### HDR rendering
- Excellent on Windows. Composition layer is DComp; per-visual color
  management; HDR swapchain in `SwapChainPanel` is well-supported.
- Materially better than `MediaPlayerElement`'s MediaFoundation HDR
  path because of libplacebo's tone mapping.

### mpv integration mechanism
- Place `SwapChainPanel` in XAML where the video should appear.
- Create `ID3D11Device` + `IDXGISwapChain1` (sized to the panel,
  HDR-configured).
- Call `ISwapChainPanelNative::SetSwapChain(swapchain)` to register
  with the WinUI compositor.
- Pass `ID3D11Device` to libmpv via `MPV_RENDER_API_TYPE_PL_D3D11`.
- Per `MPV_RENDER_UPDATE_FRAME` event: `GetBuffer(0)` →
  `ID3D11Texture2D` → `mpv_render_context_render()` with the
  texture + `MPV_RENDER_PARAM_TARGET_COLORSPACE` → `Present()`.

### UI development model
- XAML (declarative) + C# (or C++/WinRT for native).
- Composition API for advanced animation/visual effects.
- Native Windows look-and-feel; integrates with Fluent Design.
- Reuses many UWP/.NET libraries via Windows App SDK.

### Pros
- Best Windows-native polish (Fluent Design, native look-and-feel).
- Composition (DComp under) is the modern compositor — best HDR
  composition story on Windows.
- libmpv render-API integration is conceptually clean — same shape
  as Qt's QQuickRhiItem.
- Reuses .NET/UWP ecosystem (audio APIs, system integrations).
- Smaller footprint than Electron, fast cold start.

### Cons
- Windows-only. No cross-platform story.
- WinUI 3 is younger than WinUI 2 / UWP; some rough edges.
- Native interop with libmpv requires C++/WinRT or P/Invoke
  layers (libmpv is C; C# bindings exist but are community).
- Some Windows App SDK API churn between versions.

### Risks / known issues
- WinUI 3 packaging / deployment is more complex than older UWP /
  Win32 apps (sparse packages, unpackaged, MSIX...).
- ABI between Windows App SDK versions occasionally shifts.
- DComp + SwapChainPanel HDR works but is sparsely documented for
  the per-frame-texture pattern (community samples are limited).

### Existing prior art
- Fluss (this codebase) — current WinUI 3 player using
  `MediaPlayerElement`; would gain a SwapChainPanel-based mpv backend.
- Microsoft's own samples for SwapChainPanel + D3D11 (mostly games).
- Various WinUI 3 apps using SwapChainPanel for custom rendering.

---

## Option 3 — Avalonia + mpv render API

### Summary
.NET-based cross-platform XAML-style UI. Avalonia hosts mpv-rendered
content via `NativeControlHost` (HWND on Windows, equivalent on other
platforms) or lower-level rendering hooks. Lighter / earlier on the
maturity curve than Qt.

### Stack
- Avalonia 11+ (.NET 8/9 baseline).
- AXAML for UI (Avalonia's XAML dialect).
- Skia as default renderer (Skia 2D backend on each platform).
- `NativeControlHost` for native window embedding.
- libmpv + new render API + per-platform pl context-fns + a per-
  platform native control layer.

### Platforms / compatibility
- Windows / macOS / Linux. .NET 8 cross-platform.
- HDR support on each platform: relies on whatever Avalonia's Skia
  backend exposes — Windows D3D Skia is improving, macOS Metal Skia
  is mature, Linux is the weakest.
- License: MIT.

### HDR rendering
- Material risk. Avalonia's HDR story is less developed than Qt RHI
  or WinUI 3 Composition. The Skia compositor doesn't have first-
  class HDR primitives.
- Practical approach: use `NativeControlHost` to embed a native
  HDR surface per platform; Avalonia hosts a "native control hole"
  where your D3D11/Metal/Vulkan code does HDR rendering.
- UI compositing over the HDR surface: Avalonia draws around it
  (the hole is opaque); native HDR surface + Avalonia UI cannot be
  freely Z-mixed the way Qt RHI can.

### mpv integration mechanism
- `NativeControlHost`-derived class hosting a native child window
  (HWND on Windows, NSView on macOS, X11/Wayland window on Linux).
- Inside the native control: create D3D11/Metal/Vulkan device +
  swapchain, drive libmpv via the new render API.
- mpv renders into the native control's swapchain; Avalonia's
  layout positions it, but Avalonia cannot composite UI on top of
  it (Avalonia is unaware of the native pixels).
- UI overlay options: mpv OSD (HDR-aware), separate Avalonia popup
  windows over the native control hole.

### UI development model
- AXAML + C# + ReactiveUI-style bindings.
- Familiar to WPF/WinUI/Xamarin developers.
- Strong tooling via Visual Studio + Rider.

### Pros
- Cross-platform with a .NET-shaped developer experience — strong
  for teams already in .NET.
- Lower learning curve than Qt for .NET developers.
- MIT-licensed end-to-end.
- Active development; growing fast.

### Cons
- HDR maturity lags Qt RHI and WinUI 3 noticeably.
- Native control compositing model is more restrictive — you don't
  get "free" UI-over-video compositing in Avalonia the way Qt
  RhiItem provides.
- Smaller ecosystem than Qt; fewer prior-art examples for HDR media
  players.
- Avalonia 11+ is still moving fast; APIs occasionally shift.

### Risks / known issues
- Per-platform native HDR backend code is largely on you — Avalonia
  doesn't abstract D3D11/Metal/Vulkan the way Qt RHI does.
- libmpv C# bindings (Mpv.NET, etc.) are community-maintained;
  binding lifetime + thread-affinity issues with the render API are
  not pre-solved like in Qt.
- Mixing Skia composition with native control HDR has unresolved
  edge cases (transparency, animation over the native control).

### Existing prior art
- Avalonia samples for `NativeControlHost`-based video
  (libvlcsharp + Avalonia exists; mpv + Avalonia less so).
- General Avalonia apps shipping cross-platform — few are HDR
  media players.

---

## Option 4 — Hybrid: per-platform native shells + shared C++ engine

### Summary
Each platform gets its own native shell (WinUI 3 on Windows,
Cocoa/SwiftUI on macOS, Qt or GTK on Linux). All shells consume the
*same* shared C++ engine that wraps libmpv + the new render API.
Maximum per-platform polish; maximum codebase footprint.

### Stack — per platform
| Platform | UI shell | Graphics | HDR mechanism | mpv path |
|---|---|---|---|---|
| Windows | WinUI 3 | SwapChainPanel + D3D11 | DXGI HDR + DComp | render API + PL_D3D11 |
| macOS | Cocoa / SwiftUI / Catalyst | Metal CAMetalLayer | macOS EDR | render API + PL_METAL |
| Linux | Qt or GTK | Vulkan/GL | Wayland HDR (KDE 6+) | render API + PL_VULKAN |

### Shared C++ engine (the "core")
- Owns libmpv lifecycle (create, event loop, command/property API).
- Owns playback state, library/database, settings, networking,
  metadata.
- Exposes a stable C++ API to each native shell (or via C bindings
  for non-C++ shells).
- Each shell consumes the engine via thin native bindings
  (P/Invoke + C# wrappers for WinUI 3, Objective-C++ for Cocoa,
  Q_OBJECT bridging for Qt).

### Platforms / compatibility
- All three desktops, each via its native UI framework.
- Each shell is a separate codebase; common engine is shared.

### HDR rendering
- Best per-platform: each shell uses the platform's strongest HDR
  composition (DComp on Win, EDR on Mac, Wayland HDR on Linux).
- All driven through the same mpv render API on the engine side.

### Pros
- Truly native UX on each platform (Fluent Design on Windows,
  Cupertino on Mac, GTK/Qt-native on Linux).
- HDR composition uses the platform's best compositor.
- Engine code reuse keeps business logic / playback / library
  layer single-source.
- Migrating away from any one shell doesn't require rewriting
  the engine.
- Lets existing investments (e.g., Fluss for Windows) be reused
  while expanding to other platforms.

### Cons
- 3x UI codebases (or 2x, if you skip one platform initially).
- Feature parity between shells is hard to maintain.
- Team needs expertise in multiple UI frameworks (or hires per
  platform).
- Build/CI complexity grows substantially.
- More test infrastructure to maintain.

### Risks / known issues
- Native shell drift: a feature lands on one platform first, takes
  months to reach others.
- Bindings/interop layers are project-specific and need ongoing
  maintenance.
- Per-platform UI behaviors (window management, menu structure,
  keyboard shortcuts) need explicit design decisions, not unified
  defaults.

### Existing prior art
- Infuse (Apple platforms only, but exemplifies the per-platform
  native approach).
- Many large media apps use this internally even when they appear
  "cross-platform" externally.
- General software pattern (browser engines like Chromium expose
  via Cocoa shell on Mac, GTK on Linux, Win32 on Windows).

### Common engine considerations
- Language: C++ (libmpv is C; libplacebo is C; FFmpeg is C — C++
  fits naturally as a thin wrapper).
- Build: CMake or Meson for cross-platform engine; each shell uses
  its native build system to consume.
- Thread model: libmpv has clear threading rules; engine encapsulates
  them so shells don't have to think about them.
- IPC / API: engine exposes a synchronous C++ API + async event
  callbacks; shells bridge to their UI thread accordingly.

---

## Option 5 — WinUI 3 + MediaPlayerElement + FFmpegInteropX (current Fluss)

### Summary
Existing Fluss architecture. WinUI 3 XAML with `MediaPlayerElement`
driven by Windows.Media.Playback. `FFmpegInteropX` extends format
support by wrapping FFmpeg decoders behind a MediaFoundation
`IMediaStreamSource`.

### Stack
- WinUI 3 + XAML.
- `MediaPlayerElement` (Windows.Media.Playback under the hood).
- MediaFoundation as the render pipeline.
- FFmpegInteropX (WinRT library, depends on FFmpeg builds via
  vcpkg or similar) as a `MediaSource` provider for non-Store codecs.
- MS Store codecs (HEVC, AV1, others) optionally via Microsoft
  Store extensions.

### Platforms / compatibility
- Windows 10 1809+ / Windows 11 only.
- FFmpegInteropX maintained for WinRT/UWP/WinUI 3 ecosystem.
- License: WinUI 3 = MIT; FFmpeg = LGPL/GPL depending on build
  options.

### HDR rendering
- MediaFoundation grade. HDR10 playback works on HDR displays; tone
  mapping is MF's implementation (not libplacebo's). Materially
  less sophisticated than libplacebo's tone mapping algorithms
  (`bt.2390`, peak detect, gamut mapping).
- For most viewers indistinguishable; for HDR-quality-sensitive
  users (the project's target audience for "pro grade") noticeably
  inferior.

### mpv integration mechanism
- N/A — this option does NOT use mpv. Playback runs through MF.

### UI development model
- Same XAML / C# / WinUI 3 stack as Option 2.

### Pros
- Already in Fluss; zero migration cost.
- Lowest engineering complexity for adding format support
  (FFmpegInteropX is drop-in).
- Tight Windows integration (system controls, casting,
  notifications, accessibility) via MediaPlayerElement.
- Smaller memory + cold-start footprint than mpv-based options
  (no libplacebo engine; no GPU shader compilation startup).

### Cons
- HDR quality ceiling = MediaFoundation, not libplacebo.
- No `--target-prim/--target-trc/--target-peak`-style fine control.
- No custom shaders / user shaders / 3D LUT support.
- Windows-only.
- Tied to MediaFoundation's pipeline quirks (specific format
  support, system-codec dependencies).

### Risks / known issues
- FFmpegInteropX is community-maintained; releases lag major FFmpeg
  versions. Build setup is non-trivial.
- MediaFoundation HDR pipeline behavior varies across Windows
  versions and GPU drivers.
- System codec dependencies (HEVC extension purchase, etc.) are
  user-visible friction.

### Position in the player project
- Keep as the **default lightweight playback path** in Fluss.
- Pair with Option 2 (`SwapChainPanel + mpv`) as the **"high-quality
  HDR"** opt-in mode. Best of both worlds: low-overhead default for
  most files, libplacebo-grade for users who want HDR perfection.

### Existing prior art
- Fluss itself.
- Many WinUI 3 media apps in the wild use this pattern.

---

## Option 6 — Electron + DComp + mpv `--wid` (other current approach)

### Summary
Existing alternative architecture. Electron (Chromium + Node)
provides the UI; native side creates a DComp visual hosting an mpv
swapchain via `--wid=<HWND>`. Plex-Desktop-class architecture.

### Stack
- Electron (Chromium + Node).
- HTML/CSS/JS for UI (React/Vue/Svelte/whatever).
- Native module (N-API addon or node-gyp) that owns the mpv
  embedding.
- `mpv --wid=<HWND>` to a child HWND.
- The child HWND lives as a DComp visual either:
  - As a top-level layered window above Electron's window, or
  - As a child window in Electron's HWND hierarchy, or
  - As a DComp visual hosting the HWND content (less common).
- mpv's own DXGI swapchain on the HWND handles HDR.

### Platforms / compatibility
- Windows / macOS / Linux (Electron is cross-platform).
- HDR via DComp on Windows; macOS/Linux paths use mpv's native
  HDR backends (Metal/Vulkan/GL).
- License: Electron is MIT; mpv is GPL/LGPL.

### HDR rendering
- libplacebo-grade (mpv's full rendering pipeline). HDR works
  identically to `--vo=gpu-next` windowed because that *is* the
  pipeline in use.
- HDR negotiation on Windows is mpv's own (DXGI HDR via the
  windowed gpu_ctx).
- This is the architecture currently validated on the user's rig.

### mpv integration mechanism
- N/A — uses `--wid`, not the render API.
- The new HDR render API plan does NOT need to land for this
  option to work; HDR already works here today.

### UI development model
- Web: HTML, CSS, JavaScript/TypeScript.
- Vast ecosystem (React, Vue, design systems, animation libs,
  data fetching, state management — all "for free").
- Easy designer/dev separation.
- Hot reload, browser-style DevTools.

### Pros
- Best UI development velocity of any option.
- Mature, vast ecosystem.
- Cross-platform (Electron runs everywhere).
- HDR via mpv's `--wid` path already works; minimal native
  custom code needed.
- Already deployed.

### Cons
- 100-200 MB resident at idle (Chromium baseline).
- 1-2 sec cold start.
- Per-frame Chromium compositor wakeups even with no UI change.
- UI ↔ video integration is "siblings in a window," not "child
  in a scene." Animations / transformations / blends with the
  video plane are awkward.
- Chromium updates occasionally break native composition
  assumptions.

### Risks / known issues
- DComp + Electron's window is fragile to Chromium internal
  changes. Tested patterns work but evolve with Chromium.
- HDR-aware Web UI on top of DComp video plane: the HTML pixels
  are sRGB-interpreted by DWM; HDR coexistence is per-visual,
  not per-pixel.
- Native module lifetime / GC interactions with libmpv require
  care.

### Position in the player project
- The current production / shipping option (along with Fluss).
- Keep as-is unless migrating to native shells for footprint /
  polish reasons.

### Existing prior art
- Plex Desktop (now retired; was Electron-based).
- Jellyfin Media Player (Qt-based now, but used to be Electron).
- Discord, Slack, VS Code (Electron + native composition for
  specialized surfaces).

---

## Lightweight Electron alternative — Tauri (variant of Option 6)

Worth keeping on the radar even though not one of the six main
options:

- Tauri = Rust shell + system WebView (WebView2 on Windows,
  WKWebView on Mac, WebKitGTK on Linux).
- ~10-50 MB binary vs Electron's 100+ MB.
- Same DComp-video-plane + Web-UI pattern as Electron works.
- Web UI keeps Electron's ecosystem advantages without the
  Chromium footprint.
- WebView2 on Win = Chromium-based, integrates with DComp
  similarly. WebKitGTK on Linux is the weak link.
- Migration cost: low if Electron app uses standard web APIs;
  higher if it depends on Node-specific APIs heavily.
- Same mpv `--wid` HDR approach; same HDR ceiling (libplacebo-
  grade because mpv pipeline is fully active).
- No need for the new render API plan to land for Tauri.

---

## Cross-cutting comparison matrix

| Dimension | 1. Qt | 2. WinUI3 | 3. Avalonia | 4. Hybrid | 5. Fluss MPE | 6. Electron |
|---|---|---|---|---|---|---|
| Cross-platform | ✅ Win/Mac/Lin | ❌ Win-only | ✅ Win/Mac/Lin | ✅ per-platform | ❌ Win-only | ✅ Win/Mac/Lin |
| HDR quality ceiling | libplacebo | libplacebo | libplacebo | libplacebo | MediaFoundation | libplacebo |
| HDR + UI compositing | ✅ scene-graph | ✅ scene-graph | ⚠️ native-hole | ✅ best per-platform | ✅ native | ⚠️ siblings |
| Cold start | ~200ms | ~150ms | ~250ms | ~150-300ms | ~150ms | ~1-2s |
| Memory at idle | 60-100 MB | 50-80 MB | 60-100 MB | 50-100 MB | 50-80 MB | 150-300 MB |
| UI dev model | QML/QtQuick | XAML/WinUI 3 | AXAML/.NET | per-platform | XAML/WinUI 3 | HTML/CSS/JS |
| Ecosystem maturity | High | Med-High | Medium | n/a | High | Very High |
| HDR composition maturity | High (Qt 6.7+) | High (DComp) | Low | Best per-platform | n/a (MF) | High via mpv |
| Needs new mpv render API plan | YES | YES | YES | YES (per platform) | NO | NO |
| Migration cost from Fluss | High | Low (extend) | Med-High | Med (additive) | n/a (current) | High |
| License | LGPL/Commercial | MIT | MIT | mix | MIT | MIT |
| Industry adoption | Very high | Med (newer) | Growing | n/a | n/a | Very high |

---

## Decision factors (questions to answer before committing)

1. **Is cross-platform a hard requirement at launch, or year 2+?**
   - At launch → Option 1, 3, or 4 (or stay with Option 6/Tauri).
   - Year 2+ → ship Option 2 first for Windows (reusing Fluss),
     decide cross-platform later.

2. **What's the team's expertise?**
   - Web/JS-heavy → Options 5, 6, Tauri.
   - .NET-heavy → Options 2, 3, 5.
   - C++/Qt-heavy → Options 1, 4.

3. **How polished does the UI need to be on launch?**
   - "Plex/Infuse-grade" → Options 1, 2, 4 (native), or Option 6
     with React + polished design system.
   - "Functional / utility-grade" → any option.

4. **How sensitive are users to HDR quality?**
   - High-end videophile audience → libplacebo grade (Options
     1, 2, 3, 4, 6). Option 5 too weak for this audience.
   - General audience → Option 5 is fine.

5. **What's the existing Fluss code worth preserving?**
   - High value, monolithic XAML/C# → extend with Option 2
     `SwapChainPanel + mpv` as alternative backend, keep
     `MediaPlayerElement` as default.
   - Refactorable, business logic separable → consider Option 4
     hybrid (Fluss for Windows, build other platforms separately).

6. **Memory / cold-start sensitivity?**
   - Tight constraints (kiosks, embedded, low-spec machines) →
     native options (1-4). Avoid Electron.
   - Comfortable → any option.

7. **License constraints (commercial / closed-source)?**
   - Closed-source with static linking needs → check Qt
     Commercial vs LGPL.
   - mpv: GPL/LGPL — affects all options that link libmpv.
   - libplacebo: LGPL.
   - FFmpeg: LGPL by default, GPL if built with `--enable-gpl`.

---

## Common substrate work (the mpv-side plan)

For options 1-4, the same mpv-side work is the prerequisite:

- Implement `MPV_RENDER_API_TYPE_PL_D3D11` (Windows).
- Implement `MPV_RENDER_API_TYPE_PL_METAL` (macOS).
- Implement `MPV_RENDER_API_TYPE_PL_VULKAN` (Linux + cross-platform).
- Add `MPV_RENDER_PARAM_TARGET_COLORSPACE` public-API param.
- Each backend = ~150 LOC of `libmpv_pl_context_<api>.c` parallel to
  the existing `libmpv_pl_context_gl.c` from W5-6/W6.
- mpv-side: full plan in
  [mpv-src/plan-hdr-render-api.md](c:\DEV\ai-dev\projects\mpv-src\plan-hdr-render-api.md).
- D3D11 is the sequencing priority (covers Options 1-Windows,
  2-only, 4-Windows side, and is the validated rig).

---

## Validated baseline (current state, 2026-05)

The W5-6 + W6 mpv fork (HEAD `8a2df0e`) is signed off:
- Windowed `--vo=gpu-next` bit-faithful to mainline `35ae76d` on
  real D3D11 NVIDIA HDR (PNG `46CDAFFF…`, JSON `7712D507…`/
  `E70ED9BE…`, both SW and HW decode).
- `MPV_RENDER_API_TYPE_PL_OPENGL` reachable end-to-end; harness
  self-baselines byte-stably (4/4).
- This is the foundation any of options 1-4 builds on.

Worktree: `c:\DEV\ai-dev\projects\mpv-wt-8a2df0e`.

---

## Cross-references

- [plan-hdr-render-api.md](c:\DEV\ai-dev\projects\mpv-src\plan-hdr-render-api.md)
  — the mpv-side HDR render-API plan (engine-agnostic).
- [plan.md](c:\DEV\ai-dev\projects\mpv-src\plan.md) — the W5-6 + W6
  refactor plan that this whole effort built on.
- [HANDOFF.md](c:\DEV\ai-dev\projects\mpv-src\HANDOFF.md) — current
  mpv refactor status.
