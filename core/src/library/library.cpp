// arca library database: schema, import/remove, filesystem scan, listings,
// search, progress, and queue-facing read models.
// OFFLINE scan = enumeration only; ONLINE scan additionally runs the local
// indexing pass (online_index.cpp) and fills the pending-enrichment queue.
// No networking exists anywhere in this module or its includes.

#include "arca/arca_library.h"

#include "../util/json_writer.h"
#include "online_index.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <new>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct arca_db {
    sqlite3 *db = nullptr;
};

struct arca_queue {
    arca_db *db = nullptr; // borrowed; shells destroy queues before db close
    std::vector<std::string> ids;
    int current = -1;
    bool shuffle = false;
    std::vector<int> shuffle_history;
    std::vector<int> shuffle_remaining;
    std::mt19937 rng{std::random_device{}()};
};

namespace {

constexpr int kSchemaVersion = 2;

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
CREATE VIRTUAL TABLE IF NOT EXISTS media_search USING fts5(
  media_id UNINDEXED,
  library_id UNINDEXED,
  title,
  file_name,
  rel_path,
  group_key
);
CREATE TABLE IF NOT EXISTS playback_progress(
  media_id TEXT PRIMARY KEY REFERENCES media_items(id) ON DELETE CASCADE,
  position_seconds REAL NOT NULL,
  duration_seconds REAL NOT NULL,
  last_updated_utc INTEGER NOT NULL,
  is_completed INTEGER NOT NULL DEFAULT 0
);
)sql";

const std::unordered_set<std::string> kVideoExts = {
    ".mp4", ".mkv", ".mov", ".avi", ".wmv", ".m4v", ".webm",
    ".mpg", ".mpeg", ".ts", ".m2ts", ".hevc",
};

struct MediaRow {
    std::string id;
    int64_t library_id = 0;
    std::string library_name;
    int64_t library_mode = ARCA_LIB_OFFLINE;
    std::string file_name;
    std::string rel_path;
    int64_t size = 0;
    int64_t modified_utc = 0;
    int64_t added_utc = 0;
    std::optional<std::string> title;
    std::optional<int64_t> year;
    std::optional<int64_t> season;
    std::optional<int64_t> episode;
    std::optional<std::string> group_key;
};

int64_t now_utc() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string normalize_slashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (!value.empty() && value.front() == '/')
        value.erase(value.begin());
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    return value;
}

std::string lower_ascii(std::string value) {
    for (char &c : value)
        c = (char)std::tolower((unsigned char)c);
    return value;
}

std::string parent_rel_path(const std::string &rel_path) {
    std::string rel = normalize_slashes(rel_path);
    size_t slash = rel.find_last_of('/');
    return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

std::string display_title(const MediaRow &row) {
    if (row.title && !row.title->empty()) {
        if (!row.season && row.year)
            return *row.title + " (" + std::to_string(*row.year) + ")";
        return *row.title;
    }
    return row.file_name.empty() ? row.rel_path : row.file_name;
}

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
    void bind(int i, int64_t v) { sqlite3_bind_int64(stmt_, i, v); }
    void bind(int i, double v) { sqlite3_bind_double(stmt_, i, v); }
    void bind(int i, const std::string &v) {
        sqlite3_bind_text(stmt_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT);
    }
    void bind_null(int i) { sqlite3_bind_null(stmt_, i); }
    bool step_row() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    bool step_done() { return sqlite3_step(stmt_) == SQLITE_DONE; }
    void reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }

    int64_t col_i64(int i) { return sqlite3_column_int64(stmt_, i); }
    double col_double(int i) { return sqlite3_column_double(stmt_, i); }
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

MediaRow read_media_row(Stmt &st) {
    MediaRow row;
    row.id = st.col_text(0);
    row.file_name = st.col_text(1);
    row.rel_path = normalize_slashes(st.col_text(2));
    row.size = st.col_i64(3);
    row.modified_utc = st.col_i64(4);
    row.added_utc = st.col_i64(5);
    if (!st.col_null(6)) row.title = st.col_text(6);
    if (!st.col_null(7)) row.year = st.col_i64(7);
    if (!st.col_null(8)) row.season = st.col_i64(8);
    if (!st.col_null(9)) row.episode = st.col_i64(9);
    if (!st.col_null(10)) row.group_key = st.col_text(10);
    row.library_id = st.col_i64(11);
    row.library_name = st.col_text(12);
    row.library_mode = st.col_i64(13);
    return row;
}

const char *kMediaSelect =
    "SELECT m.id, m.file_name, m.rel_path, m.file_size,"
    " m.modified_utc, m.added_utc,"
    " o.parse_title, o.parse_year, o.season, o.episode, o.group_key,"
    " l.id, l.name, l.mode"
    " FROM media_items m"
    " JOIN libraries l ON l.id = m.library_id"
    " LEFT JOIN online_media_info o ON o.media_id = m.id";

std::vector<MediaRow> media_for_library(sqlite3 *db, int64_t library_id) {
    std::vector<MediaRow> rows;
    std::string sql = std::string(kMediaSelect) + " WHERE m.library_id=?";
    Stmt st(db, sql.c_str());
    st.bind(1, library_id);
    while (st.step_row())
        rows.push_back(read_media_row(st));
    return rows;
}

std::optional<MediaRow> media_by_id(sqlite3 *db, const std::string &id) {
    std::string sql = std::string(kMediaSelect) + " WHERE m.id=?";
    Stmt st(db, sql.c_str());
    st.bind(1, id);
    if (!st.step_row())
        return std::nullopt;
    return read_media_row(st);
}

void sort_media(std::vector<MediaRow> &rows, arca_sort_order sort) {
    auto title_less = [](const MediaRow &a, const MediaRow &b) {
        std::string at = lower_ascii(display_title(a));
        std::string bt = lower_ascii(display_title(b));
        return at == bt ? lower_ascii(a.rel_path) < lower_ascii(b.rel_path) : at < bt;
    };
    switch (sort) {
    case ARCA_SORT_TITLE_DESC:
        std::sort(rows.begin(), rows.end(), [&](const auto &a, const auto &b) { return title_less(b, a); });
        break;
    case ARCA_SORT_ADDED_DESC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.added_utc > b.added_utc; });
        break;
    case ARCA_SORT_ADDED_ASC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.added_utc < b.added_utc; });
        break;
    case ARCA_SORT_MODIFIED_DESC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.modified_utc > b.modified_utc; });
        break;
    case ARCA_SORT_MODIFIED_ASC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.modified_utc < b.modified_utc; });
        break;
    case ARCA_SORT_SIZE_DESC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.size > b.size; });
        break;
    case ARCA_SORT_SIZE_ASC:
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.size < b.size; });
        break;
    case ARCA_SORT_TITLE_ASC:
    default:
        std::sort(rows.begin(), rows.end(), title_less);
        break;
    }
}

void write_media_object(arca::JsonWriter &w, const MediaRow &row,
                        bool include_library, std::optional<bool> is_current = std::nullopt) {
    w.begin_object();
    w.key("id"); w.value(row.id);
    w.key("fileName"); w.value(row.file_name);
    w.key("relPath"); w.value(row.rel_path);
    w.key("folderRelPath"); w.value(parent_rel_path(row.rel_path));
    w.key("size"); w.value(row.size);
    w.key("modifiedUtc"); w.value(row.modified_utc);
    w.key("addedUtc"); w.value(row.added_utc);
    if (row.title) { w.key("title"); w.value(*row.title); }
    if (row.year) { w.key("year"); w.value(*row.year); }
    if (row.season) { w.key("season"); w.value(*row.season); }
    if (row.episode) { w.key("episode"); w.value(*row.episode); }
    if (row.group_key) { w.key("groupKey"); w.value(*row.group_key); }
    if (include_library) {
        w.key("libraryId"); w.value(row.library_id);
        w.key("libraryName"); w.value(row.library_name);
        w.key("mode"); w.value(row.library_mode == ARCA_LIB_ONLINE ? "online" : "offline");
    }
    if (is_current) {
        w.key("isCurrent"); w.value(*is_current);
    }
    w.end_object();
}

void rebuild_search_for_library(sqlite3 *db, int64_t library_id) {
    Stmt del(db, "DELETE FROM media_search WHERE library_id=?");
    del.bind(1, library_id);
    del.step_done();

    Stmt ins(db,
            "INSERT INTO media_search(media_id, library_id, title, file_name, rel_path, group_key)"
            " VALUES (?,?,?,?,?,?)");
    for (const MediaRow &row : media_for_library(db, library_id)) {
        ins.bind(1, row.id);
        ins.bind(2, row.library_id);
        ins.bind(3, row.title ? *row.title : row.file_name);
        ins.bind(4, row.file_name);
        ins.bind(5, row.rel_path);
        ins.bind(6, row.group_key ? *row.group_key : "");
        ins.step_done();
        ins.reset();
    }
}

void rebuild_all_search(sqlite3 *db) {
    Stmt libs(db, "SELECT id FROM libraries");
    while (libs.step_row())
        rebuild_search_for_library(db, libs.col_i64(0));
}

std::string build_fts_query(const char *query_utf8) {
    if (!query_utf8)
        return "";
    std::vector<std::string> tokens;
    std::string current;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(query_utf8); *p; ++p) {
        if (std::isalnum(*p)) {
            current.push_back((char)std::tolower(*p));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        tokens.push_back(current);
    std::string out;
    for (const std::string &token : tokens) {
        if (token.size() < 2)
            continue;
        if (!out.empty())
            out += " AND ";
        out += token + "*";
    }
    return out;
}

std::vector<std::string> parse_json_string_array(const char *json) {
    std::vector<std::string> values;
    if (!json)
        return values;
    const char *p = json;
    while (*p) {
        while (*p && *p != '"')
            ++p;
        if (!*p)
            break;
        ++p;
        std::string value;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                ++p;
                switch (*p) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(*p); break;
                }
            } else {
                value.push_back(*p);
            }
            ++p;
        }
        if (*p == '"')
            ++p;
        values.push_back(value);
    }
    return values;
}

void queue_refill_remaining(arca_queue *queue) {
    queue->shuffle_remaining.clear();
    for (int i = 0; i < (int)queue->ids.size(); ++i) {
        if (i != queue->current)
            queue->shuffle_remaining.push_back(i);
    }
}

void queue_reset_shuffle(arca_queue *queue) {
    queue->shuffle_history.clear();
    queue_refill_remaining(queue);
}

arca_status queue_set_current_index(arca_queue *queue, int index) {
    if (!queue || index < 0 || index >= (int)queue->ids.size())
        return ARCA_ERR_INVALID_ARG;
    queue->current = index;
    queue_reset_shuffle(queue);
    return ARCA_OK;
}

} // namespace

extern "C" {

arca_db *arca_db_open(const char *db_path_utf8) {
    if (!db_path_utf8)
        return nullptr;
    auto *handle = new (std::nothrow) arca_db();
    if (!handle)
        return nullptr;

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

    int64_t old_version = 0;
    Stmt ver(handle->db, "SELECT version FROM schema_version");
    if (ver.step_row()) {
        old_version = ver.col_i64(0);
    } else {
        Stmt ins(handle->db, "INSERT INTO schema_version(version) VALUES (?)");
        ins.bind(1, (int64_t)kSchemaVersion);
        ins.step_done();
        old_version = kSchemaVersion;
    }
    if (old_version < kSchemaVersion) {
        Stmt upd(handle->db, "UPDATE schema_version SET version=?");
        upd.bind(1, (int64_t)kSchemaVersion);
        upd.step_done();
        rebuild_all_search(handle->db);
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

    std::unordered_map<std::string, std::string> existing;
    {
        Stmt st(db->db, "SELECT rel_path, id FROM media_items WHERE library_id=?");
        st.bind(1, library_id);
        while (st.step_row())
            existing.emplace(normalize_slashes(st.col_text(0)), st.col_text(1));
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
            break;
        const fs::directory_entry &entry = *it;
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = lower_ascii(utf8_of(entry.path().extension()));
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
        std::string rel = normalize_slashes(utf8_of(fs::relative(entry.path(), root, ec)));
        if (ec || rel.empty())
            rel = normalize_slashes(utf8_of(entry.path().filename()));
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
            if (online)
                index_online_item(db->db, id, file_name);
        }
    }

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
    rebuild_search_for_library(db->db, library_id);
    exec(db->db, "COMMIT");

    if (out_added) *out_added = added;
    if (out_removed) *out_removed = removed;
    return ARCA_OK;
}

char *arca_media_list_json(arca_db *db, int64_t library_id) {
    if (!db)
        return nullptr;
    auto rows = media_for_library(db->db, library_id);
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        return lower_ascii(a.rel_path) < lower_ascii(b.rel_path);
    });

    arca::JsonWriter w;
    w.begin_array();
    for (const auto &row : rows)
        write_media_object(w, row, false);
    w.end_array();
    return dup_out(w.str());
}

char *arca_library_children_json(arca_db *db, int64_t library_id,
                                 const char *folder_rel_path_utf8,
                                 arca_sort_order sort) {
    if (!db)
        return nullptr;
    std::string folder = normalize_slashes(folder_rel_path_utf8 ? folder_rel_path_utf8 : "");
    auto rows = media_for_library(db->db, library_id);
    sort_media(rows, sort);

    std::map<std::string, int> folder_counts;
    std::vector<MediaRow> media;
    std::string prefix = folder.empty() ? "" : folder + "/";
    for (const auto &row : rows) {
        const std::string &rel = row.rel_path;
        if (!folder.empty() && rel.rfind(prefix, 0) != 0)
            continue;
        std::string rest = folder.empty() ? rel : rel.substr(prefix.size());
        size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            media.push_back(row);
        } else {
            std::string name = rest.substr(0, slash);
            std::string child = folder.empty() ? name : folder + "/" + name;
            folder_counts[child]++;
        }
    }

    arca::JsonWriter w;
    w.begin_object();
    w.key("folderRelPath"); w.value(folder);
    w.key("folders");
    w.begin_array();
    for (const auto &[rel, count] : folder_counts) {
        size_t slash = rel.find_last_of('/');
        std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
        w.begin_object();
        w.key("name"); w.value(name);
        w.key("relPath"); w.value(rel);
        w.key("itemCount"); w.value((int64_t)count);
        w.end_object();
    }
    w.end_array();
    w.key("media");
    w.begin_array();
    for (const auto &row : media)
        write_media_object(w, row, false);
    w.end_array();
    w.end_object();
    return dup_out(w.str());
}

char *arca_media_search_json(arca_db *db, const char *query_utf8,
                             int64_t library_id_or_zero, int limit) {
    if (!db)
        return nullptr;
    std::string fts = build_fts_query(query_utf8);
    arca::JsonWriter w;
    w.begin_array();
    if (!fts.empty()) {
        int capped = std::clamp(limit <= 0 ? 80 : limit, 1, 200);
        std::string sql = std::string(kMediaSelect) +
            " JOIN media_search ON media_search.media_id = m.id"
            " WHERE media_search MATCH ?"
            " AND (?=0 OR m.library_id=?)"
            " ORDER BY bm25(media_search)"
            " LIMIT ?";
        Stmt st(db->db, sql.c_str());
        st.bind(1, fts);
        st.bind(2, library_id_or_zero);
        st.bind(3, library_id_or_zero);
        st.bind(4, (int64_t)capped);
        while (st.step_row())
            write_media_object(w, read_media_row(st), true);
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

arca_status arca_progress_save(arca_db *db, const char *media_id,
                               double position_seconds, double duration_seconds,
                               bool is_completed) {
    if (!db || !media_id)
        return ARCA_ERR_INVALID_ARG;
    Stmt st(db->db,
            "INSERT INTO playback_progress"
            "(media_id, position_seconds, duration_seconds, last_updated_utc, is_completed)"
            " VALUES (?,?,?,?,?)"
            " ON CONFLICT(media_id) DO UPDATE SET"
            " position_seconds=excluded.position_seconds,"
            " duration_seconds=excluded.duration_seconds,"
            " last_updated_utc=excluded.last_updated_utc,"
            " is_completed=excluded.is_completed");
    st.bind(1, std::string(media_id));
    st.bind(2, std::max(0.0, position_seconds));
    st.bind(3, std::max(0.0, duration_seconds));
    st.bind(4, now_utc());
    st.bind(5, is_completed ? (int64_t)1 : (int64_t)0);
    return st.step_done() ? ARCA_OK : ARCA_ERR_ENGINE;
}

double arca_progress_resume_seconds(arca_db *db, const char *media_id) {
    if (!db || !media_id)
        return -1.0;
    Stmt st(db->db,
            "SELECT position_seconds, duration_seconds, is_completed"
            " FROM playback_progress WHERE media_id=?");
    st.bind(1, std::string(media_id));
    if (!st.step_row())
        return -1.0;
    double pos = st.col_double(0);
    double dur = st.col_double(1);
    bool completed = st.col_i64(2) != 0;
    if (completed || pos < 5.0 || (dur > 0.0 && pos >= dur - 5.0))
        return -1.0;
    return pos;
}

char *arca_progress_continue_watching_json(arca_db *db, int limit) {
    if (!db)
        return nullptr;
    int capped = std::clamp(limit <= 0 ? 20 : limit, 1, 100);
    std::string sql =
        "SELECT m.id, m.file_name, m.rel_path, m.file_size,"
        " m.modified_utc, m.added_utc,"
        " o.parse_title, o.parse_year, o.season, o.episode, o.group_key,"
        " l.id, l.name, l.mode,"
        " p.position_seconds, p.duration_seconds, p.last_updated_utc"
        " FROM media_items m"
        " JOIN libraries l ON l.id = m.library_id"
        " LEFT JOIN online_media_info o ON o.media_id = m.id"
        " JOIN playback_progress p ON p.media_id = m.id"
        " WHERE p.is_completed=0"
        " AND p.position_seconds >= 5"
        " AND (p.duration_seconds <= 0 OR p.position_seconds < p.duration_seconds - 5)"
        " ORDER BY p.last_updated_utc DESC LIMIT ?";
    Stmt st(db->db, sql.c_str());
    st.bind(1, (int64_t)capped);
    arca::JsonWriter w;
    w.begin_array();
    while (st.step_row()) {
        MediaRow row = read_media_row(st);
        w.begin_object();
        w.key("media"); write_media_object(w, row, true);
        w.key("positionSeconds"); w.value(st.col_double(14));
        w.key("durationSeconds"); w.value(st.col_double(15));
        w.key("lastUpdatedUtc"); w.value(st.col_i64(16));
        w.end_object();
    }
    w.end_array();
    return dup_out(w.str());
}

arca_queue *arca_queue_create(arca_db *db) {
    if (!db)
        return nullptr;
    auto *queue = new (std::nothrow) arca_queue();
    if (!queue)
        return nullptr;
    queue->db = db;
    return queue;
}

void arca_queue_destroy(arca_queue *queue) {
    delete queue;
}

arca_status arca_queue_set_from_media_ids_json(arca_queue *queue,
                                               const char *media_ids_json,
                                               const char *current_media_id) {
    if (!queue)
        return ARCA_ERR_INVALID_ARG;
    queue->ids = parse_json_string_array(media_ids_json);
    queue->current = queue->ids.empty() ? -1 : 0;
    if (current_media_id && *current_media_id) {
        auto it = std::find(queue->ids.begin(), queue->ids.end(), current_media_id);
        if (it != queue->ids.end())
            queue->current = (int)std::distance(queue->ids.begin(), it);
    }
    queue_reset_shuffle(queue);
    return ARCA_OK;
}

char *arca_queue_list_json(arca_queue *queue) {
    if (!queue)
        return nullptr;
    arca::JsonWriter w;
    w.begin_object();
    w.key("currentIndex"); w.value((int64_t)queue->current);
    w.key("shuffle"); w.value(queue->shuffle);
    w.key("items");
    w.begin_array();
    for (int i = 0; i < (int)queue->ids.size(); ++i) {
        if (auto row = media_by_id(queue->db->db, queue->ids[i])) {
            write_media_object(w, *row, true, i == queue->current);
        } else {
            w.begin_object();
            w.key("id"); w.value(queue->ids[i]);
            w.key("missing"); w.value(true);
            w.key("isCurrent"); w.value(i == queue->current);
            w.end_object();
        }
    }
    w.end_array();
    w.end_object();
    return dup_out(w.str());
}

char *arca_queue_current_json(arca_queue *queue) {
    if (!queue || queue->current < 0 || queue->current >= (int)queue->ids.size())
        return nullptr;
    if (auto row = media_by_id(queue->db->db, queue->ids[queue->current])) {
        arca::JsonWriter w;
        write_media_object(w, *row, true, true);
        return dup_out(w.str());
    }
    return nullptr;
}

char *arca_queue_current_media_id(arca_queue *queue) {
    if (!queue || queue->current < 0 || queue->current >= (int)queue->ids.size())
        return nullptr;
    return dup_out(queue->ids[queue->current]);
}

arca_status arca_queue_set_current(arca_queue *queue, const char *media_id) {
    if (!queue || !media_id)
        return ARCA_ERR_INVALID_ARG;
    auto it = std::find(queue->ids.begin(), queue->ids.end(), media_id);
    if (it == queue->ids.end())
        return ARCA_ERR_INVALID_ARG;
    return queue_set_current_index(queue, (int)std::distance(queue->ids.begin(), it));
}

arca_status arca_queue_next(arca_queue *queue) {
    if (!queue || queue->current < 0)
        return ARCA_ERR_INVALID_ARG;
    if (queue->shuffle) {
        if (queue->shuffle_remaining.empty())
            queue_refill_remaining(queue);
        if (queue->shuffle_remaining.empty())
            return ARCA_ERR_UNSUPPORTED;
        std::uniform_int_distribution<int> dist(0, (int)queue->shuffle_remaining.size() - 1);
        int pick = dist(queue->rng);
        queue->shuffle_history.push_back(queue->current);
        queue->current = queue->shuffle_remaining[pick];
        queue->shuffle_remaining.erase(queue->shuffle_remaining.begin() + pick);
        return ARCA_OK;
    }
    if (queue->current >= (int)queue->ids.size() - 1)
        return ARCA_ERR_UNSUPPORTED;
    queue->current++;
    return ARCA_OK;
}

arca_status arca_queue_previous(arca_queue *queue) {
    if (!queue || queue->current < 0)
        return ARCA_ERR_INVALID_ARG;
    if (queue->shuffle) {
        if (queue->shuffle_history.empty())
            return ARCA_ERR_UNSUPPORTED;
        queue->shuffle_remaining.push_back(queue->current);
        queue->current = queue->shuffle_history.back();
        queue->shuffle_history.pop_back();
        return ARCA_OK;
    }
    if (queue->current <= 0)
        return ARCA_ERR_UNSUPPORTED;
    queue->current--;
    return ARCA_OK;
}

arca_status arca_queue_set_shuffle(arca_queue *queue, bool enabled) {
    if (!queue)
        return ARCA_ERR_INVALID_ARG;
    queue->shuffle = enabled && queue->ids.size() > 1;
    queue_reset_shuffle(queue);
    return ARCA_OK;
}

bool arca_queue_shuffle(arca_queue *queue) {
    return queue ? queue->shuffle : false;
}

} // extern "C"
