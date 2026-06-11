// arca_engine.h — playback engine control plane.
//
// One arca_engine owns one libmpv instance and an internal event thread.
// Event callbacks fire on that internal thread; shells must marshal to
// their UI thread. Day 0 runs software decode by design (DECISIONS.md
// ADR-003): hwdec on the HDR render path is a deferred engine-side phase.

#ifndef ARCA_ENGINE_H
#define ARCA_ENGINE_H

#include "arca.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arca_engine arca_engine;

typedef enum arca_play_state {
    ARCA_PLAY_IDLE    = 0,  // no file
    ARCA_PLAY_LOADING = 1,  // file requested, not yet decoding
    ARCA_PLAY_PLAYING = 2,
    ARCA_PLAY_PAUSED  = 3,
    ARCA_PLAY_ENDED   = 4,  // end of file reached (engine holds last frame)
} arca_play_state;

typedef enum arca_event_kind {
    ARCA_EVENT_STATE_CHANGED  = 1,  // .state
    ARCA_EVENT_POSITION       = 2,  // .seconds (playback position)
    ARCA_EVENT_DURATION       = 3,  // .seconds (may arrive late or change)
    ARCA_EVENT_FILE_LOADED    = 4,  // decoding started; properties readable
    ARCA_EVENT_PLAYBACK_ERROR = 5,  // .message
    ARCA_EVENT_LOG            = 6,  // .message (engine log line)
    ARCA_EVENT_SHUTDOWN       = 7,  // engine is going away
} arca_event_kind;

typedef struct arca_event {
    arca_event_kind kind;
    arca_play_state state;    // STATE_CHANGED
    double seconds;           // POSITION / DURATION
    const char *message;      // PLAYBACK_ERROR / LOG; valid only inside the callback
} arca_event;

// Fired on the engine's internal event thread.
typedef void (*arca_event_callback)(const arca_event *event, void *userdata);

typedef struct arca_engine_params {
    arca_event_callback on_event;  // optional
    void *userdata;
    bool verbose_log;              // forward verbose engine logs (default: info level)
} arca_engine_params;

ARCA_API arca_engine *arca_engine_create(const arca_engine_params *params);

// Destroys the engine. Any render session attached to it is destroyed first.
ARCA_API void arca_engine_destroy(arca_engine *engine);

ARCA_API arca_status arca_engine_load(arca_engine *engine, const char *path_utf8);
ARCA_API arca_status arca_engine_stop(arca_engine *engine);
ARCA_API arca_status arca_engine_set_paused(arca_engine *engine, bool paused);
ARCA_API arca_status arca_engine_toggle_paused(arca_engine *engine);
ARCA_API arca_status arca_engine_seek_absolute(arca_engine *engine, double seconds);
ARCA_API arca_status arca_engine_seek_relative(arca_engine *engine, double delta_seconds);

ARCA_API arca_play_state arca_engine_state(arca_engine *engine);
ARCA_API double arca_engine_position(arca_engine *engine);  // seconds; < 0 if unknown
ARCA_API double arca_engine_duration(arca_engine *engine);  // seconds; < 0 if unknown

ARCA_API arca_status arca_engine_set_volume(arca_engine *engine, double volume_0_100);
ARCA_API double arca_engine_volume(arca_engine *engine);
ARCA_API arca_status arca_engine_set_muted(arca_engine *engine, bool muted);
ARCA_API bool arca_engine_muted(arca_engine *engine);

// Diagnostic access to any engine property (libmpv property namespace; e.g.
// "video-out-params", "hwdec-current", "frame-drop-count"). Returns NULL if
// unavailable; caller frees with arca_string_free. Verification tooling and
// debug overlays only — shells must not build features on raw properties.
ARCA_API char *arca_engine_get_property_string(arca_engine *engine, const char *name);

// Diagnostic/verification escape hatch: run a raw engine command line
// (libmpv syntax, e.g. "screenshot-to-file dump.png video"). Same caveat.
ARCA_API arca_status arca_engine_command_string(arca_engine *engine, const char *command);

#ifdef __cplusplus
}
#endif

#endif // ARCA_ENGINE_H
