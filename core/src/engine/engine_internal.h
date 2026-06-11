// Internal engine state shared between engine.cpp and render_d3d11.cpp.
// Not part of the public ABI.

#ifndef ARCA_ENGINE_INTERNAL_H
#define ARCA_ENGINE_INTERNAL_H

#include "arca/arca_engine.h"

#include <mpv/client.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct arca_render_session;

struct arca_engine {
    mpv_handle *mpv = nullptr;

    arca_event_callback on_event = nullptr;
    void *userdata = nullptr;

    std::thread event_thread;
    std::atomic<bool> quitting{false};

    // Cached playback state (written by the event thread, read by anyone).
    std::atomic<double> position{-1.0};
    std::atomic<double> duration{-1.0};
    std::atomic<int> state{ARCA_PLAY_IDLE};

    // State derivation inputs (event thread only).
    bool prop_idle = true;
    bool prop_pause = false;
    bool prop_eof = false;
    bool loading = false;

    // The one render session attached to this engine (render_d3d11.cpp).
    // Guarded by session_mutex; engine destroy tears it down first.
    std::mutex session_mutex;
    arca_render_session *session = nullptr;

    void emit(const arca_event &ev) const {
        if (on_event)
            on_event(&ev, userdata);
    }
    void emit_log(const char *msg) const {
        arca_event ev{};
        ev.kind = ARCA_EVENT_LOG;
        ev.message = msg;
        emit(ev);
    }
};

// Implemented in render_d3d11.cpp; called by arca_engine_destroy.
void arca_render_detach_for_engine_destroy(arca_engine *engine);

#endif // ARCA_ENGINE_INTERNAL_H
