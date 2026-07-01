// lib-verify — exercises the library ABI end-to-end against a scratch
// directory tree: open/migrate DB, add offline + online libraries, scan,
// list JSON, path lookup, rescan stability, delete w/ cascade.
// Exit 0 = all checks pass. The M3/M4 core gate.

#include <arca/arca.h>
#include <arca/arca_library.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond, what)                                              \
    do {                                                               \
        if (cond) {                                                    \
            std::printf("ok   %s\n", what);                            \
        } else {                                                       \
            std::printf("FAIL %s\n", what);                            \
            g_failures++;                                              \
        }                                                              \
    } while (0)

static bool contains(const char *json, const char *needle) {
    return json && std::strstr(json, needle) != nullptr;
}

static std::vector<std::string> extract_ids(const char *json) {
    std::vector<std::string> ids;
    const char *p = json;
    while (p && (p = std::strstr(p, "\"id\":\"")) != nullptr) {
        p += 6;
        const char *end = std::strchr(p, '"');
        if (!end)
            break;
        ids.emplace_back(p, end - p);
        p = end + 1;
    }
    return ids;
}

static void touch(const fs::path &p, const char *content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

int main() {
    fs::path scratch = fs::temp_directory_path() / "arca_libverify";
    fs::remove_all(scratch);

    // Scratch media tree.
    fs::path offline_root = scratch / "offline";
    touch(offline_root / "clips" / "a.mkv");
    touch(offline_root / "clips" / "b.mp4");
    touch(offline_root / "notes.txt");  // ignored (not a video ext)
    fs::path online_root = scratch / "online";
    touch(online_root / "Show.Name.S01E01.1080p.x265.mkv");
    touch(online_root / "Show.Name.S01E02.1080p.x265.mkv");
    touch(online_root / "The.Departed.2006.2160p.DV.HDR.x265.mkv");

    fs::path db_path = scratch / "test.db";
    arca_db *db = arca_db_open(db_path.string().c_str());
    CHECK(db != nullptr, "db open + schema");
    if (!db)
        return 1;

    int64_t off = arca_library_add(db, "Offline", offline_root.string().c_str(),
                                   ARCA_LIB_OFFLINE);
    int64_t on = arca_library_add(db, "Online", online_root.string().c_str(),
                                  ARCA_LIB_ONLINE);
    CHECK(off > 0 && on > 0, "library add (both modes)");
    CHECK(arca_library_add(db, "Dup", offline_root.string().c_str(),
                           ARCA_LIB_OFFLINE) < 0,
          "duplicate root rejected");

    int added = 0, removed = 0;
    CHECK(arca_library_scan(db, off, &added, &removed) == ARCA_OK &&
              added == 2 && removed == 0,
          "offline scan: 2 videos found, txt ignored");
    CHECK(arca_library_scan(db, on, &added, &removed) == ARCA_OK && added == 3,
          "online scan: 3 videos found");

    char *libs = arca_library_list_json(db);
    CHECK(contains(libs, "\"mode\":\"offline\"") &&
              contains(libs, "\"mode\":\"online\"") &&
              contains(libs, "\"itemCount\":2") &&
              contains(libs, "\"itemCount\":3"),
          "library list JSON (modes + counts)");
    arca_string_free(libs);

    char *off_media = arca_media_list_json(db, off);
    CHECK(contains(off_media, "a.mkv") && contains(off_media, "b.mp4") &&
              !contains(off_media, "groupKey"),
          "offline media list: explorer-flat, no online fields");

    char *on_media = arca_media_list_json(db, on);
    CHECK(contains(on_media, "\"title\":\"Show Name\"") &&
              contains(on_media, "\"season\":1") &&
              contains(on_media, "\"episode\":2") &&
              contains(on_media, "\"groupKey\":\"show name\""),
          "online media list: series parsed + grouped");
    CHECK(contains(on_media, "\"title\":\"The Departed\"") &&
              contains(on_media, "\"year\":2006") &&
              contains(on_media, "\"groupKey\":\"the departed (2006)\""),
          "online media list: movie title/year parsed");

    // Path lookup round-trip for the first offline item id.
    auto off_ids = extract_ids(off_media);
    auto on_ids = extract_ids(on_media);
    std::string id = off_ids.empty() ? std::string() : off_ids[0];
    char *path = arca_media_get_path(db, id.c_str());
    CHECK(path && fs::exists(fs::path(std::u8string(path, path + std::strlen(path)))), "media path lookup resolves");
    arca_string_free(path);

    char *tools_json = arca_media_tools_status_json(db);
    CHECK(contains(tools_json, "\"cacheRoot\"") &&
              contains(tools_json, "\"ffprobeAvailable\"") &&
              contains(tools_json, "\"ffmpegAvailable\""),
          "media tools status JSON");
    arca_string_free(tools_json);

    char *detail_json = arca_media_detail_json(db, id.c_str());
    CHECK(contains(detail_json, "\"absolutePath\"") &&
              contains(detail_json, "\"probe\"") &&
              contains(detail_json, "\"status\":\"missing\"") &&
              contains(detail_json, "\"thumbnails\""),
          "media detail JSON before probe");
    arca_string_free(detail_json);

    char *root_children = arca_library_children_json(db, off, "", ARCA_SORT_TITLE_ASC);
    CHECK(contains(root_children, "\"folders\"") && contains(root_children, "\"name\":\"clips\"") &&
              !contains(root_children, "\"fileName\":\"a.mkv\""),
          "children root: folder only at top level");
    arca_string_free(root_children);

    char *clip_children = arca_library_children_json(db, off, "clips", ARCA_SORT_TITLE_ASC);
    CHECK(contains(clip_children, "\"fileName\":\"a.mkv\"") &&
              contains(clip_children, "\"fileName\":\"b.mp4\""),
          "children folder: immediate media listed");
    arca_string_free(clip_children);

    char *search = arca_media_search_json(db, "departed", 0, 10);
    CHECK(contains(search, "The Departed") && contains(search, "\"libraryName\":\"Online\""),
          "fts search: finds online parsed title");
    arca_string_free(search);

    char *browse = arca_media_browse_json(db, "", 8, 24);
    CHECK(contains(browse, "\"filters\"") &&
              contains(browse, "\"sections\"") &&
              contains(browse, "The Departed") &&
              contains(browse, "Show Name"),
          "browse JSON: filters + mixed sections");
    arca_string_free(browse);

    char *browse_movies = arca_media_browse_json(db, "movies", 8, 24);
    CHECK(contains(browse_movies, "The Departed") &&
              !contains(browse_movies, "Show Name"),
          "browse JSON: movies filter");
    arca_string_free(browse_movies);

    char *browse_series = arca_media_browse_json(db, "series", 8, 24);
    CHECK(contains(browse_series, "Show Name") &&
              !contains(browse_series, "The Departed"),
          "browse JSON: series filter");
    arca_string_free(browse_series);

    if (!on_ids.empty()) {
        CHECK(arca_progress_save(db, on_ids[0].c_str(), 42.0, 120.0, false) == ARCA_OK,
              "progress save");
        CHECK(arca_progress_resume_seconds(db, on_ids[0].c_str()) >= 41.9,
              "progress resume point");
        char *continue_json = arca_progress_continue_watching_json(db, 5);
        CHECK(contains(continue_json, "\"positionSeconds\":42") &&
                  contains(continue_json, on_ids[0].c_str()),
              "continue watching JSON");
        arca_string_free(continue_json);
    }

    arca_queue *queue = arca_queue_create(db);
    CHECK(queue != nullptr, "queue create");
    if (queue && on_ids.size() >= 2) {
        std::string queue_ids = "[\"" + on_ids[0] + "\",\"" + on_ids[1] + "\"]";
        CHECK(arca_queue_set_from_media_ids_json(queue, queue_ids.c_str(), on_ids[0].c_str()) == ARCA_OK,
              "queue set from JSON ids");
        char *queue_json = arca_queue_list_json(queue);
        CHECK(contains(queue_json, "\"currentIndex\":0") &&
                  contains(queue_json, "\"isCurrent\":true"),
              "queue list JSON marks current");
        arca_string_free(queue_json);
        CHECK(arca_queue_next(queue) == ARCA_OK, "queue next");
        char *current_id = arca_queue_current_media_id(queue);
        CHECK(current_id && std::strcmp(current_id, on_ids[1].c_str()) == 0,
              "queue current after next");
        arca_string_free(current_id);
        CHECK(arca_queue_previous(queue) == ARCA_OK, "queue previous");
        CHECK(arca_queue_set_shuffle(queue, true) == ARCA_OK && arca_queue_shuffle(queue),
              "queue shuffle toggle");
    }
    arca_queue_destroy(queue);

    arca_string_free(off_media);
    arca_string_free(on_media);

    // Rescan stability + removal detection.
    fs::remove(offline_root / "clips" / "b.mp4");
    CHECK(arca_library_scan(db, off, &added, &removed) == ARCA_OK &&
              added == 0 && removed == 1,
          "rescan: deletion detected, no phantom adds");

    CHECK(arca_library_remove(db, on) == ARCA_OK, "library remove");
    char *libs2 = arca_library_list_json(db);
    CHECK(!contains(libs2, "\"mode\":\"online\""), "remove cascaded");
    arca_string_free(libs2);

    arca_db_close(db);
    fs::remove_all(scratch);

    std::printf("lib-verify: %s (%d failures)\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
