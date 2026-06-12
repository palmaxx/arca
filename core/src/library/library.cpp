// arca library database: schema, import/remove, filesystem scan, listings.
// OFFLINE scan = enumeration only; ONLINE scan additionally runs the local
// indexing pass (online_index.cpp) and fills the pending-enrichment queue.
// No networking exists anywhere in this module or its includes — that is
// the Day-0 hard seam (arca_library.h).

#include "arca/arca_library.h"

#include "../util/json_writer.h"
#include "online_index.h"

#include <sqlite3.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct arca_db {
    sqlite3 *db = nullptr;
};

namespace {

constexpr int kSchemaVersion = 1;

const char *kSchema = R"sql(
CREATE TABLE IF NOT EXISTS schema_version(version INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS libraries(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  root_path TEXT NOT NULL UNIQUE,
  mode INTEGER NOT NULL,
  created_utc INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS media_items(
  id TEXT PRIMARY KEY,
  library_id INTEGER NOT NULL REFERENCES libraries(id) ON DELETE CASCADE,
  rel_path TEXT NOT NULL,
  file_name TEXT NOT NULL,
  file_size INTEGER NOT NULL,
  modified_utc INTEGER NOT NULL,
  added_utc INTEGER NOT NULL,
  UNIQUE(library_id, rel_path)
);
CREATE INDEX IF NOT EXISTS idx_media_library ON media_items(library_id);
CREATE TABLE IF NOT EXISTS online_media_info(
  media_id TEXT PRIMARY KEY REFERENCES media_items(id) ON DELETE CASCADE,
  parse_title TEXT,
  parse_year INTEGER,
  season INTEGER,
  episode INTEGER,
  group_key TEXT,
  enrich_status TEXT NOT NULL DEFAULT 'pending'
);
)sql";

// Indexed video extensions (reference: Fluss README + streamxs indexer).
const std::unordered_set<std::string> kVideoExts = {
    ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".m4v", ".webm",
    ".mpg", ".mpeg", ".ts", ".m2ts", ".hevc",
};

int64_t now_utc() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Content-derived media id (Fluss MediaIdHelper concept): FNV-1a 64 over
// "abs_path|size", hex-encoded.
std::string media_id_for(const std::string &abs_path, uint64_t size) {
    std::string key = abs_path + "|" + std::to_string(size);
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

bool exec(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

class Stmt {
public:
    Stmt(sqlite3 *db, const char *sql) {
        sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
    }
    ~Stmt() { sqlite3_finalize(stmt_); }
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    bool ok() const { return stmt_ != nullptr; }
    sqlite3_stmt *get() { return stmt_; }

    void bind(int i, int64_t v) { sqlite3_bind_int64(stmt_, i, v); }
    void bind(int i, const std::string &v) {
        sqlite3_bind_text(stmt_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
    }
    void bind_null(int i) { sqlite3_bind_null(stmt_, i); }
    bool step_row() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    bool step_done() { return sqlite3_step(stmt_) == SQLITE_DONE; }
    void reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }

    int64_t col_i64(int i) { return sqlite3_column_int64(stmt_, i); }
    bool col_null(int i) { return sqlite3_column_type(stmt_, i) == SQLITE_NULL; }
    std::string col_text(int i) {
        const unsigned char *t = sqlite3_column_text(stmt_, i);
        return t ? reinterpret_cast<const char *>(t) : "";
    }

private:
    sqlite3_stmt *stmt_ = nullptr;
};

char *dup_out(const std::string &s) {
    char *out = (char *)malloc(s.size() + 1);
    if (out)
        memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

fs::path path_from_utf8(const std::string &s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string utf8_of(const fs::path &p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// Runs the ONLINE-library local indexing pass for one item (queue insert;
// 'pending' = awaiting the future fetcher phase).
void index_online_item(sqlite3 *db, const std::string &media_id,
                       const std::string &file_name) {
    arca::ParsedName p = arca::parse_media_filename(file_name);
    Stmt st(db,
            "INSERT OR REPLACE INTO online_media_info"
            "(media_id, parse_title, parse_year, season, episode, group_key,"
            " enrich_status) VALUES (?,?,?,?,?,?,'pending')");
    st.bind(1, media_id);
    st.bind(2, p.title);
    if (p.year) st.bind(3, (int64_t)*p.year); else st.bind_null(3);
    if (p.season) st.bind(4, (int64_t)*p.season); else st.bind_null(4);
    if (p.episode) st.bind(5, (int64_t)*p.episode); else st.bind_null(5);
    st.bind(6, p.group_key);
    st.step_done();
}

} // namespace

extern "C" {

arca_db *arca_db_open(const char *db_path_utf8) {
    if (!db_path_utf8)
        return nullptr;
    auto *handle = new (std::nothrow) arca_db();
    if (!handle)
        return nullptr;

    // Serialized mode: callable from any shell thread.
    if (sqlite3_open_v2(db_path_utf8, &handle->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                        SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        sqlite3_close(handle->db);
        delete handle;
        return nullptr;
    }
    exec(handle->db, "PRAGMA journal_mode=WAL");
    exec(handle->db, "PRAGMA foreign_keys=ON");
    if (!exec(handle->db, kSchema)) {
        sqlite3_close(handle->db);
        delete handle;
        return nullptr;
    }
    Stmt ver(handle->db, "SELECT version FROM schema_version");
    if (!ver.step_row()) {
        Stmt ins(handle->db, "INSERT INTO schema_version(version) VALUES (?)");
        ins.bind(1, (int64_t)kSchemaVersion);
        ins.step_done();
    }
    return handle;
}

void arca_db_close(arca_db *db) {
    if (!db)
        return;
    sqlite3_close(db->db);
    delete db;
}

int64_t arca_library_add(arca_db *db, const char *name_utf8,
                         const char *root_path_utf8, arca_library_mode mode) {
    if (!db || !name_utf8 || !root_path_utf8)
        return -1;
    std::error_code ec;
    fs::path root = fs::absolute(path_from_utf8(root_path_utf8), ec);
    if (ec || !fs::is_directory(root, ec))
        return -1;

    Stmt st(db->db, "INSERT INTO libraries(name, root_path, mode, created_utc)"
                    " VALUES (?,?,?,?)");
    st.bind(1, std::string(name_utf8));
    st.bind(2, utf8_of(root));
    st.bind(3, (int64_t)mode);
    st.bind(4, now_utc());
    if (!st.step_done())
        return -1;
    return sqlite3_last_insert_rowid(db->db);
}

arca_status arca_library_remove(arca_db *db, int64_t library_id) {
    if (!db)
        return ARCA_ERR_INVALID_ARG;
    Stmt st(db->db, "DELETE FROM libraries WHERE id=?");
    st.bind(1, library_id);
    return st.step_done() ? ARCA_OK : ARCA_ERR_ENGINE;
}

char *arca_library_list_json(arca_db *db) {
    if (!db)
        return nullptr;
    Stmt st(db->db,
            "SELECT l.id, l.name, l.root_path, l.mode,"
            " (SELECT COUNT(*) FROM media_items m WHERE m.library_id = l.id)"
            " FROM libraries l ORDER BY l.name");
    arca::JsonWriter w;
    w.begin_array();
    while (st.step_row()) {
        w.begin_object();
        w.key("id"); w.value(st.col_i64(0));
        w.key("name"); w.value(st.col_text(1));
        w.key("rootPath"); w.value(st.col_text(2));
        w.key("mode"); w.value(st.col_i64(3) == ARCA_LIB_ONLINE ? "online" : "offline");
        w.key("itemCount"); w.value(st.col_i64(4));
        w.end_object();
    }
    w.end_array();
    return dup_out(w.str());
}

arca_status arca_library_scan(arca_db *db, int64_t library_id,
                              int *out_added, int *out_removed) {
    if (out_added) *out_added = 0;
    if (out_removed) *out_removed = 0;
    if (!db)
        return ARCA_ERR_INVALID_ARG;

    Stmt lib(db->db, "SELECT root_path, mode FROM libraries WHERE id=?");
    lib.bind(1, library_id);
    if (!lib.step_row())
        return ARCA_ERR_INVALID_ARG;
    fs::path root = path_from_utf8(lib.col_text(0));
    bool online = lib.col_i64(1) == ARCA_LIB_ONLINE;

    // Existing rows: rel_path -> id.
    std::unordered_map<std::string, std::string> existing;
    {
        Stmt st(db->db, "SELECT rel_path, id FROM media_items WHERE library_id=?");
        st.bind(1, library_id);
        while (st.step_row())
            existing.emplace(st.col_text(0), st.col_text(1));
    }

    exec(db->db, "BEGIN");
    int added = 0;
    std::unordered_set<std::string> seen;
    Stmt upsert(db->db,
                "INSERT INTO media_items"
                "(id, library_id, rel_path, file_name, file_size,"
                " modified_utc, added_utc) VALUES (?,?,?,?,?,?,?)"
                " ON CONFLICT(library_id, rel_path) DO UPDATE SET"
                " id=excluded.id, file_size=excluded.file_size,"
                " modified_utc=excluded.modified_utc");

    std::error_code ec;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        exec(db->db, "ROLLBACK");
        return ARCA_ERR_INVALID_ARG;
    }
    for (auto end = fs::end(it); it != end; it.increment(ec)) {
        if (ec)
            break;  // iterator failure: keep what we have
        const fs::directory_entry &entry = *it;
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = utf8_of(entry.path().extension());
        for (char &c : ext)
            c = (char)tolower((unsigned char)c);
        if (!kVideoExts.count(ext))
            continue;

        uint64_t size = entry.file_size(ec);
        if (ec)
            continue;
        auto mtime = entry.last_write_time(ec);
        int64_t mtime_utc = ec ? 0
            : std::chrono::duration_cast<std::chrono::seconds>(
                  mtime.time_since_epoch()).count();

        std::string abs = utf8_of(entry.path());
        std::string rel = utf8_of(fs::relative(entry.path(), root, ec));
        if (ec || rel.empty())
            rel = utf8_of(entry.path().filename());
        std::string file_name = utf8_of(entry.path().filename());
        std::string id = media_id_for(abs, size);

        seen.insert(rel);
        auto prev = existing.find(rel);
        bool is_new = prev == existing.end();
        bool changed = !is_new && prev->second != id;
        if (is_new || changed) {
            upsert.bind(1, id);
            upsert.bind(2, library_id);
            upsert.bind(3, rel);
            upsert.bind(4, file_name);
            upsert.bind(5, (int64_t)size);
            upsert.bind(6, mtime_utc);
            upsert.bind(7, now_utc());
            upsert.step_done();
            upsert.reset();
            if (is_new)
                added++;
            // ONLINE pipeline only: local index + pending-enrichment queue.
            if (online)
                index_online_item(db->db, id, file_name);
        }
    }

    // Remove vanished files.
    int removed = 0;
    {
        Stmt del(db->db, "DELETE FROM media_items WHERE library_id=? AND rel_path=?");
        for (const auto &[rel, id] : existing) {
            if (!seen.count(rel)) {
                del.bind(1, library_id);
                del.bind(2, rel);
                del.step_done();
                del.reset();
                removed++;
            }
        }
    }
    exec(db->db, "COMMIT");

    if (out_added) *out_added = added;
    if (out_removed) *out_removed = removed;
    return ARCA_OK;
}

char *arca_media_list_json(arca_db *db, int64_t library_id) {
    if (!db)
        return nullptr;
    Stmt st(db->db,
            "SELECT m.id, m.file_name, m.rel_path, m.file_size,"
            " o.parse_title, o.parse_year, o.season, o.episode, o.group_key"
            " FROM media_items m"
            " LEFT JOIN online_media_info o ON o.media_id = m.id"
            " WHERE m.library_id=? ORDER BY m.rel_path");
    st.bind(1, library_id);

    arca::JsonWriter w;
    w.begin_array();
    while (st.step_row()) {
        w.begin_object();
        w.key("id"); w.value(st.col_text(0));
        w.key("fileName"); w.value(st.col_text(1));
        w.key("relPath"); w.value(st.col_text(2));
        w.key("size"); w.value(st.col_i64(3));
        if (!st.col_null(4)) {
            w.key("title"); w.value(st.col_text(4));
            if (!st.col_null(5)) { w.key("year"); w.value(st.col_i64(5)); }
            if (!st.col_null(6)) { w.key("season"); w.value(st.col_i64(6)); }
            if (!st.col_null(7)) { w.key("episode"); w.value(st.col_i64(7)); }
            w.key("groupKey"); w.value(st.col_text(8));
        }
        w.end_object();
    }
    w.end_array();
    return dup_out(w.str());
}

char *arca_media_get_path(arca_db *db, const char *media_id) {
    if (!db || !media_id)
        return nullptr;
    Stmt st(db->db,
            "SELECT l.root_path, m.rel_path FROM media_items m"
            " JOIN libraries l ON l.id = m.library_id WHERE m.id=?");
    st.bind(1, std::string(media_id));
    if (!st.step_row())
        return nullptr;
    fs::path abs = path_from_utf8(st.col_text(0)) / path_from_utf8(st.col_text(1));
    return dup_out(utf8_of(abs));
}

} // extern "C"
