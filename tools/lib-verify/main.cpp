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
    std::string id;
    if (const char *p = std::strstr(off_media, "\"id\":\"")) {
        p += 6;
        id.assign(p, std::strchr(p, '"') - p);
    }
    char *path = arca_media_get_path(db, id.c_str());
    CHECK(path && fs::exists(fs::path(std::u8string(path, path + std::strlen(path)))), "media path lookup resolves");
    arca_string_free(path);
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
