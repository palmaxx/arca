// arca_engine — control plane over libmpv (create/load/transport/properties)
// plus the internal event thread that derives play state and forwards events.

#include "engine_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

// Property-observation reply IDs.
enum : uint64_t {
    OBS_PAUSE = 1,
    OBS_TIME_POS = 2,
    OBS_DURATION = 3,
    OBS_IDLE_ACTIVE = 4,
    OBS_EOF_REACHED = 5,
};

arca_status from_mpv(int err) {
    return err >= 0 ? ARCA_OK : ARCA_ERR_ENGINE;
}

// %LOCALAPPDATA%\Arca\cache\shaders, created on demand. Returns false if the
// location can't be resolved/created (engine then just runs uncached).
bool expand_cache_dir(char *buf, size_t len) {
    const char *base = std::getenv("LOCALAPPDATA");
    if (!base)
        return false;
    std::error_code ec;
    std::filesystem::path dir =
        std::filesystem::path(base) / "Arca" / "cache" / "shaders";
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;
    std::string s = dir.string();
    if (s.size() + 1 > len)
        return false;
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return true;
}

void update_state(arca_engine *e) {
    arca_play_state next;
    if (e->loading)
        next = ARCA_PLAY_LOADING;
    else if (e->prop_idle)
        next = ARCA_PLAY_IDLE;
    else if (e->prop_eof)
        next = ARCA_PLAY_ENDED;
    else
        next = e->prop_pause ? ARCA_PLAY_PAUSED : ARCA_PLAY_PLAYING;

    if (e->state.exchange(next) != next) {
        arca_event ev{};
        ev.kind = ARCA_EVENT_STATE_CHANGED;
        ev.state = next;
        e->emit(ev);
    }
}

void handle_property(arca_engine *e, const mpv_event *mev) {
    auto *prop = static_cast<mpv_event_property *>(mev->data);
    switch (mev->reply_userdata) {
    case OBS_PAUSE:
        if (prop->format == MPV_FORMAT_FLAG)
            e->prop_pause = *static_cast<int *>(prop->data) != 0;
        update_state(e);
        break;
    case OBS_IDLE_ACTIVE:
        if (prop->format == MPV_FORMAT_FLAG)
            e->prop_idle = *static_cast<int *>(prop->data) != 0;
        update_state(e);
        break;
    case OBS_EOF_REACHED:
        // Becomes unavailable (format NONE) between files; treat as false.
        e->prop_eof = prop->format == MPV_FORMAT_FLAG &&
                      *static_cast<int *>(prop->data) != 0;
        update_state(e);
        break;
    case OBS_TIME_POS: {
        double v = prop->format == MPV_FORMAT_DOUBLE
                       ? *static_cast<double *>(prop->data) : -1.0;
        e->position.store(v);
        arca_event ev{};
        ev.kind = ARCA_EVENT_POSITION;
        ev.seconds = v;
        e->emit(ev);
        break;
    }
    case OBS_DURATION: {
        double v = prop->format == MPV_FORMAT_DOUBLE
                       ? *static_cast<double *>(prop->data) : -1.0;
        e->duration.store(v);
        arca_event ev{};
        ev.kind = ARCA_EVENT_DURATION;
        ev.seconds = v;
        e->emit(ev);
        break;
    }
    }
}

void event_loop(arca_engine *e) {
    while (!e->quitting.load()) {
        mpv_event *mev = mpv_wait_event(e->mpv, 1.0);
        switch (mev->event_id) {
        case MPV_EVENT_NONE:
            break;
        case MPV_EVENT_SHUTDOWN: {
            arca_event ev{};
            ev.kind = ARCA_EVENT_SHUTDOWN;
            e->emit(ev);
            return;
        }
        case MPV_EVENT_START_FILE:
            e->loading = true;
            update_state(e);
            break;
        case MPV_EVENT_FILE_LOADED: {
            e->loading = false;
            update_state(e);
            arca_event ev{};
            ev.kind = ARCA_EVENT_FILE_LOADED;
            e->emit(ev);
            break;
        }
        case MPV_EVENT_END_FILE: {
            e->loading = false;
            auto *ef = static_cast<mpv_event_end_file *>(mev->data);
            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                arca_event ev{};
                ev.kind = ARCA_EVENT_PLAYBACK_ERROR;
                ev.message = mpv_error_string(ef->error);
                e->emit(ev);
            }
            update_state(e);
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            auto *lm = static_cast<mpv_event_log_message *>(mev->data);
            char buf[1024];
            std::snprintf(buf, sizeof(buf), "[%s] %s: %s",
                          lm->level, lm->prefix, lm->text);
            // mpv log lines end with '\n'; trim for clean forwarding.
            size_t n = std::strlen(buf);
            if (n && buf[n - 1] == '\n')
                buf[n - 1] = '\0';
            e->emit_log(buf);
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE:
            handle_property(e, mev);
            break;
        default:
            break;
        }
    }
}

} // namespace

extern "C" {

const char *arca_version_string(void) {
    return "arca-core 0.1.0";
}

void arca_string_free(char *s) {
    std::free(s);
}

arca_engine *arca_engine_create(const arca_engine_params *params) {
    auto *e = new (std::nothrow) arca_engine();
    if (!e)
        return nullptr;
    if (params) {
        e->on_event = params->on_event;
        e->userdata = params->userdata;
    }

    e->mpv = mpv_create();
    if (!e->mpv) {
        delete e;
        return nullptr;
    }

    // The render API front-end; the gpu-next backend is selected by the
    // render-context API type at session creation (arca_render.h).
    mpv_set_option_string(e->mpv, "vo", "libmpv");
    // Self-contained engine: no user config, no terminal, no default binds.
    mpv_set_option_string(e->mpv, "config", "no");
    mpv_set_option_string(e->mpv, "terminal", "no");
    mpv_set_option_string(e->mpv, "osc", "no");
    mpv_set_option_string(e->mpv, "input-default-bindings", "no");
    // Engine outlives files; EOF holds the last frame (ARCA_PLAY_ENDED).
    mpv_set_option_string(e->mpv, "idle", "yes");
    mpv_set_option_string(e->mpv, "keep-open", "yes");
    // ADR-003: the pl-d3d11 render path is software-decode-only in the
    // current engine fork; pin it so behavior is deterministic for the
    // verification gate. Revisit when render-API hwdec lands engine-side.
    mpv_set_option_string(e->mpv, "hwdec", "no");
    // With config=no there is no default shader cache, so libplacebo
    // recompiles every launch (measured: ~6s of VO drops on first frames).
    // Persist compiled shaders under local app data.
    char cache_dir[512];
    if (expand_cache_dir(cache_dir, sizeof(cache_dir)))
        mpv_set_option_string(e->mpv, "gpu-shader-cache-dir", cache_dir);

    if (mpv_initialize(e->mpv) < 0) {
        mpv_destroy(e->mpv);
        delete e;
        return nullptr;
    }

    mpv_request_log_messages(e->mpv,
                             params && params->verbose_log ? "v" : "info");
    mpv_observe_property(e->mpv, OBS_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(e->mpv, OBS_TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(e->mpv, OBS_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(e->mpv, OBS_IDLE_ACTIVE, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(e->mpv, OBS_EOF_REACHED, "eof-reached", MPV_FORMAT_FLAG);

    e->event_thread = std::thread(event_loop, e);
    return e;
}

void arca_engine_destroy(arca_engine *engine) {
    if (!engine)
        return;

    // Unload first so the VO isn't mid-playback when its render context
    // disappears (otherwise the engine reports a spurious VO-init failure).
    const char *stop_cmd[] = {"stop", nullptr};
    mpv_command(engine->mpv, stop_cmd);

    // Render context must be freed before the mpv handle goes away.
    arca_render_detach_for_engine_destroy(engine);

    engine->quitting.store(true);
    mpv_wakeup(engine->mpv);
    if (engine->event_thread.joinable())
        engine->event_thread.join();

    mpv_terminate_destroy(engine->mpv);
    delete engine;
}

arca_status arca_engine_load(arca_engine *engine, const char *path_utf8) {
    if (!engine || !path_utf8)
        return ARCA_ERR_INVALID_ARG;
    const char *cmd[] = {"loadfile", path_utf8, nullptr};
    return from_mpv(mpv_command(engine->mpv, cmd));
}

arca_status arca_engine_stop(arca_engine *engine) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    const char *cmd[] = {"stop", nullptr};
    return from_mpv(mpv_command(engine->mpv, cmd));
}

arca_status arca_engine_set_paused(arca_engine *engine, bool paused) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    int flag = paused ? 1 : 0;
    return from_mpv(mpv_set_property(engine->mpv, "pause", MPV_FORMAT_FLAG, &flag));
}

arca_status arca_engine_toggle_paused(arca_engine *engine) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    const char *cmd[] = {"cycle", "pause", nullptr};
    return from_mpv(mpv_command(engine->mpv, cmd));
}

arca_status arca_engine_seek_absolute(arca_engine *engine, double seconds) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    char arg[64];
    std::snprintf(arg, sizeof(arg), "%.3f", seconds);
    const char *cmd[] = {"seek", arg, "absolute", nullptr};
    return from_mpv(mpv_command(engine->mpv, cmd));
}

arca_status arca_engine_seek_relative(arca_engine *engine, double delta_seconds) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    char arg[64];
    std::snprintf(arg, sizeof(arg), "%.3f", delta_seconds);
    const char *cmd[] = {"seek", arg, "relative", nullptr};
    return from_mpv(mpv_command(engine->mpv, cmd));
}

arca_play_state arca_engine_state(arca_engine *engine) {
    return engine ? static_cast<arca_play_state>(engine->state.load())
                  : ARCA_PLAY_IDLE;
}

double arca_engine_position(arca_engine *engine) {
    return engine ? engine->position.load() : -1.0;
}

double arca_engine_duration(arca_engine *engine) {
    return engine ? engine->duration.load() : -1.0;
}

arca_status arca_engine_set_volume(arca_engine *engine, double volume_0_100) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    return from_mpv(mpv_set_property(engine->mpv, "volume",
                                     MPV_FORMAT_DOUBLE, &volume_0_100));
}

double arca_engine_volume(arca_engine *engine) {
    double v = -1.0;
    if (engine)
        mpv_get_property(engine->mpv, "volume", MPV_FORMAT_DOUBLE, &v);
    return v;
}

arca_status arca_engine_set_muted(arca_engine *engine, bool muted) {
    if (!engine)
        return ARCA_ERR_INVALID_ARG;
    int flag = muted ? 1 : 0;
    return from_mpv(mpv_set_property(engine->mpv, "mute", MPV_FORMAT_FLAG, &flag));
}

bool arca_engine_muted(arca_engine *engine) {
    int flag = 0;
    if (engine)
        mpv_get_property(engine->mpv, "mute", MPV_FORMAT_FLAG, &flag);
    return flag != 0;
}

char *arca_engine_get_property_string(arca_engine *engine, const char *name) {
    if (!engine || !name)
        return nullptr;
    char *value = nullptr;
    if (mpv_get_property(engine->mpv, name, MPV_FORMAT_STRING, &value) < 0)
        return nullptr;
    // Re-home into our allocator so arca_string_free is uniform.
    char *out = _strdup(value);
    mpv_free(value);
    return out;
}

arca_status arca_engine_command_string(arca_engine *engine, const char *command) {
    if (!engine || !command)
        return ARCA_ERR_INVALID_ARG;
    return from_mpv(mpv_command_string(engine->mpv, command));
}

} // extern "C"
