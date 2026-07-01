# Playback smoke checklist

Run before declaring a playback/windowing change stable (ported from the
streamxs manual checklist, adapted to ARCA). Automatable items are covered
by `hdr-verify`/`lib-verify`; this list is the interactive pass. The mpv fork
verification scripts golden-gate render context behavior only. They are not a
substitute for these shell/UI checks.

## Engine / render (hdr-verify or shell)

- [ ] Open a local HEVC HDR file; video renders and audio continues after
      playback starts (no silent-video / black-audio split).
- [ ] `hdr-verify <clip> --seconds 10` exits 0 with zero steady-state drops.
- [ ] `hdr-verify <clip> --seek-test` passes.
- [ ] Title bar shows `[HDR <peak> nits]` when the display is in HDR mode;
      no HDR flag when it isn't (SDR fallback path).
- [ ] Drag-resize the window continuously during playback — no freeze, no
      `ResizeBuffers` errors in the debug log, video keeps presenting.
- [ ] Pause → window resize → frame redraws at the new size (forced redraw
      path).

## Shell

- [ ] Space / ← → / Shift+← → / ↑ ↓ / M / F11 all work without clicking
      a control first.
- [ ] Scrub the seek slider while playing: position follows the thumb, no
      slider snap-back fight, playback resumes smoothly on release.
- [ ] F11 fullscreen hides menu/transport/sidebar; F11 again restores all
      three (sidebar only if it was visible before).
- [ ] File → Open loads a file while another is playing (clean replace, no
      teardown error).
- [ ] Close the window mid-playback: process exits cleanly (engine stop →
      render detach ordering).

## Library

- [ ] Add an OFFLINE library: items appear explorer-flat; status line shows
      mode; double-click plays.
- [ ] Add an ONLINE library over series-named files: episodes grouped under
      the show header with SxxEyy prefixes.
- [ ] Rescan after deleting a file from disk: item disappears (`-1`).
- [ ] Remove a library: items vanish; re-adding the same root works.
- [ ] Restart the app: libraries persist, first library auto-selected.

## Fluss-parity UI slice

- [ ] Home shows continue-watching rows after playing at least one file long
      enough to save progress.
- [ ] Browse opens from the left nav, filter chips update rows, and clicking a
      media card opens Media Detail; the card play button starts playback.
- [ ] Global search navigates to Search and returns matching media from the
      core FTS index.
- [ ] Media Detail shows local probe facts and thumbnails after ffprobe/ffmpeg
      are available; missing tools are called out in Settings.
- [ ] Library and Search rows open Media Detail on row/card click and still
      offer an explicit play button.
- [ ] Queue page shows the currently selected queue and next/previous follow
      the same order.
- [ ] Window chrome matches the intended Fluss shape: custom title bar, menu,
      left nav, and focused player view.
- [ ] In Player, title and transport controls appear as overlays over the video
      surface, wake on pointer/keyboard/control use, and stop taking clicks
      after hiding.

## macOS scaffold

- [ ] On a Mac, `cd shells/macos/ArcaMac && swift run` opens the stub SwiftUI
      app.
- [ ] Sidebar navigation, Browse, Library, Search, Player placeholder, Queue,
      and Settings are all reachable.
- [ ] Browse filter chips switch rows and selecting a card opens Player.
- [ ] Player placeholder describes the Vulkan over MoltenVK path, not native
      Metal.
