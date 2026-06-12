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

// JSON, ordered by relPath:
// [{"id":"…","fileName":"…","relPath":"…","size":123,
//   "title":"…","year":2006,"season":1,"episode":2,"groupKey":"…"}]
// (title/year/season/episode/groupKey only present for ONLINE libraries.)
ARCA_API char *arca_media_list_json(arca_db *db, int64_t library_id);

// Absolute filesystem path for playback; NULL if unknown id.
ARCA_API char *arca_media_get_path(arca_db *db, const char *media_id);

#ifdef __cplusplus
}
#endif

#endif // ARCA_LIBRARY_H
