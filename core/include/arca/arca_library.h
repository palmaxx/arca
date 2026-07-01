// arca_library.h — library database: import, scan, list, delete.
//
// OFFLINE/ONLINE is a hard behavioral boundary (brief §7, PLAN.md M4):
//   - OFFLINE libraries are file-explorer-like. Their code path performs
//     filesystem enumeration ONLY — by construction it can never issue a
//     network request (the Day-0 core links no networking facility at all).
//   - ONLINE libraries additionally run the local indexing pass (filename
//     parse -> grouping) and queue items in `online_media_info` with
//     enrich_status='pending'. The fetcher that consumes that queue is a
//     later roadmap phase; today nothing consumes it (scaffolded seam).
//
// List results cross the ABI as UTF-8 JSON (caller frees with
// arca_string_free): stable, language-neutral, and trivially consumed by
// both shells. Handles are thread-safe (serialized SQLite).

#ifndef ARCA_LIBRARY_H
#define ARCA_LIBRARY_H

#include "arca.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arca_db arca_db;

typedef enum arca_library_mode {
    ARCA_LIB_OFFLINE = 0,
    ARCA_LIB_ONLINE = 1,
} arca_library_mode;

typedef enum arca_sort_order {
    ARCA_SORT_TITLE_ASC = 0,
    ARCA_SORT_TITLE_DESC = 1,
    ARCA_SORT_ADDED_DESC = 2,
    ARCA_SORT_ADDED_ASC = 3,
    ARCA_SORT_MODIFIED_DESC = 4,
    ARCA_SORT_MODIFIED_ASC = 5,
    ARCA_SORT_SIZE_DESC = 6,
    ARCA_SORT_SIZE_ASC = 7,
} arca_sort_order;

typedef struct arca_queue arca_queue;

// Opens (creating/migrating as needed) the library database.
ARCA_API arca_db *arca_db_open(const char *db_path_utf8);
ARCA_API void arca_db_close(arca_db *db);

// Returns the new library id, or -1 on failure (e.g. duplicate root path).
ARCA_API int64_t arca_library_add(arca_db *db, const char *name_utf8,
                                  const char *root_path_utf8,
                                  arca_library_mode mode);

// Removes the library and all its items (cascade).
ARCA_API arca_status arca_library_remove(arca_db *db, int64_t library_id);

// JSON: [{"id":1,"name":"…","rootPath":"…","mode":"offline","itemCount":42}]
ARCA_API char *arca_library_list_json(arca_db *db);

// Synchronous filesystem scan (call off the UI thread). Upserts found video
// files, removes vanished rows; ONLINE libraries then run the local
// indexing pass. Returns counts via the out params (optional).
ARCA_API arca_status arca_library_scan(arca_db *db, int64_t library_id,
                                       int *out_added, int *out_removed);

// JSON media object fields:
// {"id":"…","fileName":"…","relPath":"…","folderRelPath":"…","size":123,
//  "modifiedUtc":123,"addedUtc":123,"libraryId":1,"libraryName":"…",
//  "mode":"offline"}
// ONLINE libraries may additionally include title/year/season/episode/groupKey.
// Probed media may additionally include thumbnailPath, durationSeconds,
// resolution, dynamicRange, and probeStatus.
ARCA_API char *arca_media_list_json(arca_db *db, int64_t library_id);

// JSON:
// {"folderRelPath":"…",
//  "folders":[{"name":"Season 1","relPath":"Show/Season 1","itemCount":8}],
//  "media":[{media object, immediate children only}]}
ARCA_API char *arca_library_children_json(arca_db *db, int64_t library_id,
                                          const char *folder_rel_path_utf8,
                                          arca_sort_order sort);

// JSON: media objects joined with libraryId/libraryName/mode.
// Empty/NULL query returns [].
ARCA_API char *arca_media_search_json(arca_db *db, const char *query_utf8,
                                      int64_t library_id_or_zero, int limit);

// JSON browse model for shell home/browse surfaces:
// {"selectedFilter":"all","filters":[...],"sections":[...]}.
// Filters are local-only read facets over indexed items (all/movies/series/
// offline/online); no network enrichment is performed.
ARCA_API char *arca_media_browse_json(arca_db *db, const char *filter_utf8,
                                      int row_limit, int item_limit);

// Absolute filesystem path for playback; NULL if unknown id.
ARCA_API char *arca_media_get_path(arca_db *db, const char *media_id);

// Local media intelligence. These functions never call online services:
// probing/thumbnails are derived from the local file through ffprobe/ffmpeg.
// `arca_media_probe` updates cached probe fields and optionally regenerates
// three thumbnail slots. `arca_library_probe_missing` probes stale/missing
// rows for one library up to `limit` (<=0 means a small default batch).
ARCA_API arca_status arca_media_probe(arca_db *db, const char *media_id,
                                      bool generate_thumbnails);
ARCA_API arca_status arca_library_probe_missing(arca_db *db, int64_t library_id,
                                                int limit,
                                                int *out_probed,
                                                int *out_failed);
// JSON object:
// {"media":{...},"absolutePath":"…","probe":{...},"thumbnails":["…"],
//  "resumeSeconds":12.3|null}
ARCA_API char *arca_media_detail_json(arca_db *db, const char *media_id);
// JSON tool status for Settings: resolved paths, availability flags, cache root.
ARCA_API char *arca_media_tools_status_json(arca_db *db);

// Resume/continue-watching state. `resume_seconds` returns <0 when no useful
// resume point exists (completed, near start, or near end).
ARCA_API arca_status arca_progress_save(arca_db *db, const char *media_id,
                                        double position_seconds,
                                        double duration_seconds,
                                        bool is_completed);
ARCA_API double arca_progress_resume_seconds(arca_db *db, const char *media_id);
// JSON: [{"media":{media object},"positionSeconds":12.3,
//         "durationSeconds":123.0,"lastUpdatedUtc":123}]
ARCA_API char *arca_progress_continue_watching_json(arca_db *db, int limit);

// In-memory playback queue owned by the core. The shell may present it, but
// order/current/shuffle logic stays behind this ABI.
ARCA_API arca_queue *arca_queue_create(arca_db *db);
ARCA_API void arca_queue_destroy(arca_queue *queue);
ARCA_API arca_status arca_queue_set_from_media_ids_json(arca_queue *queue,
                                                       const char *media_ids_json,
                                                       const char *current_media_id);
ARCA_API char *arca_queue_list_json(arca_queue *queue);
ARCA_API char *arca_queue_current_json(arca_queue *queue);
ARCA_API char *arca_queue_current_media_id(arca_queue *queue);
ARCA_API arca_status arca_queue_set_current(arca_queue *queue, const char *media_id);
ARCA_API arca_status arca_queue_next(arca_queue *queue);
ARCA_API arca_status arca_queue_previous(arca_queue *queue);
ARCA_API arca_status arca_queue_set_shuffle(arca_queue *queue, bool enabled);
ARCA_API bool arca_queue_shuffle(arca_queue *queue);

#ifdef __cplusplus
}
#endif

#endif // ARCA_LIBRARY_H
