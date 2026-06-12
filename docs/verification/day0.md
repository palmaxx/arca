# Day 0 verification record (M2 gate, ADR-003)

Environment: Windows 11, hybrid GPU laptop — NVIDIA RTX 4060 Laptop GPU +
Intel UHD Graphics (panel owned by iGPU), internal HDR display (peak 271
nits per `IDXGIOutput6::GetDesc1`), display in Windows HDR mode.
Driver: `hdr-verify` (consumes only the public arca C ABI). Engine:
fork `gpu-next-render-api-hdr` @ `304f611f5`, `pl-d3d11`, **software decode**
(ADR-003). All runs `--seconds N` unattended with a programmatic resize
mid-run.

## M2a — Dolby Vision RPU passthrough: **GO** (2026-06-12)

**Sample (real content):** The Departed 2006 4K BDRemux (HEVC 10-bit,
BT.2020/PQ, ~75 Mbps, **DV profile 8.1**, `rpu_present_flag=1`, no EL).

Evidence of end-to-end RPU decode + consumption through the core:
- Decoder format: `3840x2160 yuv420p10 dolbyvision/bt.2020/pq/limited` —
  FFmpeg attaches DOVI metadata from the RPU NALs.
- Full filter chain and VO reconfig all carry `dolbyvision`
  (`vo/libmpv: reconfig to … dolbyvision/bt.2020/pq …`).
- `video-params`/`video-out-params`: `"colormatrix":"dolbyvision"` with
  **dynamic, RPU-derived L1 stats** — `max-pq-y 0.508181`, `avg-pq-y
  0.300122`, `max-cll 452`, `min-luma 0.000104` — values only obtainable
  from decoded RPUs (container/static metadata says 1000/0.005).
- libplacebo v7.360.1 active on the render path; vendored runtime includes
  `libdovi` (see third_party/mpv/PROVENANCE.md).

**Synthetic regression assets** (repeatable, no personal content):
`testdata/local/dv81_4k24.mkv` + `dv81_4k60.mkv` — x265-encoded HDR10 base
layers + `dovi_tool generate`d P8.1 RPUs (`inject-rpu`), muxed with
mkvmerge; ffprobe confirms `dv_profile 8.1, rpu=1`. Recipe in this repo's
history; regenerate with dovi_tool + the MSYS2 ffmpeg.

**Deferred to user-run:** DV **profile 5** (IPT-PQ-c2) — not synthesizable
and no local sample; verify with real P5 content when available.
On-display visual parity vs windowed `mpv --vo=gpu-next` is also user-run
(HDR screenshots are not valid evidence — fork Phase-5 finding).

## M2b (partial) — SW-decode headroom + adapter findings (2026-06-12)

| Run (adapter) | Content | Steady-state VO drops | Decoder drops |
|---|---|---|---|
| Intel UHD (default adapter 0) | HDR10 4K24 golden clip | 0 after ~6s warmup (~36 total) | 0 |
| Intel UHD | Departed DV8.1 4K24 75 Mbps | 0 after ~12s warmup (47 total) | 0 |
| Intel UHD | synthetic DV8.1 **4K60** | **~46/s, unbounded** — iGPU cannot tone-map 4K60 | 0 |
| **RTX 4060** | synthetic DV8.1 4K60 | **0** after ~4s warmup (35 total) | 0 |
| **RTX 4060** | Departed DV8.1 4K24 75 Mbps | **0 from t=2s, incl. through resize** | 0 |

Conclusions baked into the core:
1. **CPU software decode is NOT the bottleneck on this rig** (decoder drops
   0 everywhere, incl. 75 Mbps 4K24 and 4K60). ADR-003's deferral stands.
2. The render device must be the **high-performance adapter** — implemented
   via `EnumAdapterByGpuPreference(HIGH_PERFORMANCE)`; default adapter 0 on
   hybrid laptops is the iGPU and fails 4K60.
3. Display HDR caps must be located by **HMONITOR across all adapters**
   (swapchain `GetContainingOutput` fails cross-adapter and silently loses
   the real display peak → wrong tone-map target).
4. NVIDIA lacks `blit_dst` on wrapped R10G10B10A2 → engine runs
   `--border-background=none`; the core clears its own target
   (vendor-caps-proof).

## M2b — seek robustness + audio (2026-06-12): **PASS**

`hdr-verify --seek-test` on the 2h16m compressed DV8.1 Departed
(`G:\dv_departed\The.Departed.2006_compressed_DV.mkv`):
- Absolute seeks to 30% (2723.6s), 5% (453.9s), 90% (8170.9s), and 1.0s —
  **all landed exactly on target**, playback resumed advancing after the
  final seek, **zero VO/decoder drops across the entire run incl. seeks**.
- Audio confirmed: AC-3 5.1 (`5.1(side)`, 48 kHz float) through **WASAPI**.

## Addendum (2026-06-13) — engine fix P5b.1 landed; direct backbuffer path re-validated

The wrap-lifetime defect behind ADR-004 was fixed at source (fork commits
`008434c` + `dc7b021`, all fork gates green). ARCA re-vendored the engine
and retired the intermediate-texture indirection: the render session now
wraps the backbuffer directly with a transient RTV for the core-side border
clear. Re-validation on the RTX 4060: mid-playback resize applies cleanly
(1584x861 → 1264x681), DV8.1 4K24 zero drops + seek test PASS, 4K60 zero
steady-state drops (29-frame warmup, flat for 14s warm), `lib-verify`
13/13, shell panel path playing DV with `hdr_active=1`. The `blit_dst`
border-clear workaround (`--border-background=none` + core clear) remains —
that limitation is libplacebo-side and untouched by P5b.1.

## Remaining for full M2 sign-off (user-run)

- [ ] DV **profile 5** sample through `hdr-verify` (no local sample; not
      synthesizable — needs real IPT-PQ-c2 content).
- [ ] On-display **visual parity** vs windowed mpv: run side-by-side
      `hdr-verify <clip>` and `mpv --vo=gpu-next --gpu-api=d3d11
      --target-colorspace-hint=yes <clip>` (fork build in
      `mpv-wt-hdr\bld`) on the HDR display. Screenshots are not valid
      evidence; judge on-display (fork Phase-5 rule).
