// hdr-verify — standalone HDR/DV playback verification host for the ARCA core.
//
//   hdr-verify <clip> [--seconds N] [--verbose]
//
// Consumes ONLY the public arca C ABI (no direct engine/libmpv access) — it
// is both the M0 "first light" gate and the M2 verification driver:
//   - plays the clip into a Win32 window through the core's pl-d3d11 session
//   - dumps video-params / video-out-params / hwdec / target info after load
//   - with --seconds N: runs unattended, exercises a programmatic resize
//     halfway through (ResizeBuffers regression check), prints frame-drop
//     stats, exits 0 on clean playback / 1 on load or playback error.
//
// Interactive keys: Esc quit, Space pause, Left/Right seek 5s, Up/Down 60s.

#include <arca/arca.h>
#include <arca/arca_engine.h>
#include <arca/arca_render.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

static arca_engine *g_engine;
static arca_render_session *g_session;
static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_loaded{false};
static std::atomic<bool> g_error{false};
static bool g_verbose;

static void on_event(const arca_event *ev, void *) {
    switch (ev->kind) {
    case ARCA_EVENT_FILE_LOADED:
        g_loaded.store(true);
        break;
    case ARCA_EVENT_PLAYBACK_ERROR:
        std::fprintf(stderr, "PLAYBACK ERROR: %s\n", ev->message);
        g_error.store(true);
        break;
    case ARCA_EVENT_LOG:
        if (g_verbose)
            std::fprintf(stderr, "%s\n", ev->message);
        break;
    case ARCA_EVENT_STATE_CHANGED:
        if (g_verbose)
            std::fprintf(stderr, "state -> %d\n", (int)ev->state);
        break;
    default:
        break;
    }
}

static void dump_property(const char *name) {
    char *v = arca_engine_get_property_string(g_engine, name);
    std::printf("  %-18s %s\n", name, v ? v : "(unavailable)");
    arca_string_free(v);
}

static void dump_diagnostics(const char *label) {
    std::printf("--- %s ---\n", label);
    dump_property("video-format");
    dump_property("video-codec");
    dump_property("video-params");
    dump_property("video-out-params");
    dump_property("hwdec-current");
    dump_property("audio-codec");
    dump_property("audio-params");
    dump_property("current-ao");
    dump_property("frame-drop-count");
    dump_property("vo-delayed-frame-count");
    dump_property("estimated-vf-fps");

    arca_render_target_info info{};
    if (arca_render_get_target_info(g_session, &info) == ARCA_OK) {
        std::printf("  target: %dx%d, hdr_active=%d, display peak=%.0f nits, "
                    "min=%.4f nits\n", info.width, info.height,
                    info.hdr_active ? 1 : 0, info.display_max_nits,
                    info.display_min_nits);
    }
    std::fflush(stdout);
}

// Seek-robustness pass (M2 gate): absolute seeks across the timeline, each
// verified to land near its target (keyframe tolerance) with playback alive
// afterwards. Blocking poll is fine here: the render thread lives in the
// core and keeps presenting regardless.
static bool run_seek_test() {
    double duration = arca_engine_duration(g_engine);
    for (int i = 0; i < 100 && duration <= 0; i++) {
        Sleep(100);
        duration = arca_engine_duration(g_engine);
    }
    if (duration <= 0) {
        std::printf("seek-test: FAIL (no duration)\n");
        return false;
    }

    const double tolerance = 15.0;  // keyframe seeks land on keyframes
    const double targets[] = {duration * 0.3, duration * 0.05,
                              duration * 0.9, 1.0};
    for (double target : targets) {
        arca_engine_seek_absolute(g_engine, target);
        bool landed = false;
        for (int i = 0; i < 60 && !landed; i++) {  // up to 3s per seek
            Sleep(50);
            double pos = arca_engine_position(g_engine);
            landed = pos >= 0 && std::abs(pos - target) <= tolerance;
            if (g_error.load())
                break;
        }
        std::printf("  seek -> %.1fs: %s (pos %.1fs)\n", target,
                    landed ? "ok" : "FAIL", arca_engine_position(g_engine));
        if (!landed || g_error.load()) {
            std::printf("seek-test: FAIL\n");
            return false;
        }
    }

    // Playback must keep advancing after the final seek.
    double before = arca_engine_position(g_engine);
    Sleep(1500);
    double after = arca_engine_position(g_engine);
    bool advancing = after > before;
    std::printf("seek-test: %s (resumed %.1fs -> %.1fs)\n",
                advancing ? "PASS" : "FAIL", before, after);
    return advancing;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && g_session)
            arca_render_resize(g_session, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE: g_quit.store(true); PostQuitMessage(0); break;
        case VK_SPACE:  arca_engine_toggle_paused(g_engine); break;
        case VK_LEFT:   arca_engine_seek_relative(g_engine, -5.0); break;
        case VK_RIGHT:  arca_engine_seek_relative(g_engine, 5.0); break;
        case VK_DOWN:   arca_engine_seek_relative(g_engine, -60.0); break;
        case VK_UP:     arca_engine_seek_relative(g_engine, 60.0); break;
        }
        return 0;
    case WM_CLOSE:
        g_quit.store(true);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(int argc, char **argv) {
    const char *clip = nullptr;
    int run_seconds = 0;
    bool seek_test = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            run_seconds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--verbose"))
            g_verbose = true;
        else if (!std::strcmp(argv[i], "--seek-test"))
            seek_test = true;
        else if (!clip)
            clip = argv[i];
    }
    if (!clip) {
        std::fprintf(stderr, "usage: hdr-verify <clip> [--seconds N] "
                             "[--seek-test] [--verbose]\n");
        return 2;
    }

    std::printf("%s | clip: %s\n", arca_version_string(), clip);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"arca_hdr_verify";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"ARCA hdr-verify (pl-d3d11)  [Esc to quit]",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        1600, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        std::fprintf(stderr, "FATAL: CreateWindow failed\n");
        return 1;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);

    arca_engine_params params{};
    params.on_event = on_event;
    params.verbose_log = g_verbose;
    g_engine = arca_engine_create(&params);
    if (!g_engine) {
        std::fprintf(stderr, "FATAL: arca_engine_create failed\n");
        return 1;
    }

    g_session = arca_render_create_hwnd(g_engine, hwnd,
                                        rc.right - rc.left, rc.bottom - rc.top);
    if (!g_session) {
        std::fprintf(stderr, "FATAL: arca_render_create_hwnd failed\n");
        arca_engine_destroy(g_engine);
        return 1;
    }

    if (arca_engine_load(g_engine, clip) != ARCA_OK) {
        std::fprintf(stderr, "FATAL: load failed\n");
        arca_engine_destroy(g_engine);
        return 1;
    }

    ULONGLONG start = GetTickCount64();
    bool dumped = false, resized = false;
    while (!g_quit.load()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                g_quit.store(true);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit.load() || g_error.load())
            break;

        if (g_loaded.load() && !dumped) {
            dumped = true;
            dump_diagnostics("after load");
            if (seek_test && !run_seek_test())
                g_error.store(true);
        }

        ULONGLONG elapsed_ms = GetTickCount64() - start;
        // Sample drop counters every 2s so drop *distribution* is visible
        // (constant rate = pacing bug; bursts = event-correlated).
        static ULONGLONG next_sample = 2000;
        if (run_seconds > 0 && elapsed_ms >= next_sample) {
            next_sample += 2000;
            char *drops = arca_engine_get_property_string(g_engine, "frame-drop-count");
            char *dec = arca_engine_get_property_string(g_engine, "decoder-frame-drop-count");
            std::printf("t=%llus vo-drops=%s dec-drops=%s\n",
                        elapsed_ms / 1000, drops ? drops : "?", dec ? dec : "?");
            std::fflush(stdout);
            arca_string_free(drops);
            arca_string_free(dec);
        }
        if (run_seconds > 0) {
            // Halfway through: programmatic resize to regression-test
            // ResizeBuffers against engine-held backbuffer references.
            if (!resized && elapsed_ms > (ULONGLONG)run_seconds * 500) {
                resized = true;
                SetWindowPos(hwnd, nullptr, 0, 0, 1280, 720,
                             SWP_NOMOVE | SWP_NOZORDER);
                std::printf("(programmatic resize to 1280x720 issued)\n");
            }
            if (elapsed_ms > (ULONGLONG)run_seconds * 1000)
                break;
        }
        Sleep(10);
    }

    int rc_exit = 1;
    if (g_loaded.load() && !g_error.load()) {
        dump_diagnostics("at exit");
        rc_exit = 0;
    }

    arca_render_destroy(g_session);
    arca_engine_destroy(g_engine);
    DestroyWindow(hwnd);
    std::printf("hdr-verify exit: %d\n", rc_exit);
    return rc_exit;
}
