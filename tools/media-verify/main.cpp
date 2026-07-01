// media-verify — optional ffprobe/ffmpeg gate for media details and previews.
// Exits 0 with SKIP when tools or local sample media are unavailable.

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

static std::string first_id(const char *json) {
    const char *p = json ? std::strstr(json, "\"id\":\"") : nullptr;
    if (!p)
        return {};
    p += 6;
    const char *end = std::strchr(p, '"');
    return end ? std::string(p, end - p) : std::string();
}

static fs::path default_clip() {
    fs::path p = fs::current_path() / "testdata" / "local" / "dv81_4k24.mkv";
    if (fs::is_regular_file(p))
        return p;
    return {};
}

static void touch(const fs::path &p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << "x";
}

int main(int argc, char **argv) {
    fs::path clip = argc > 1 ? fs::path(argv[1]) : default_clip();
    if (clip.empty() || !fs::is_regular_file(clip)) {
        std::printf("media-verify: SKIP (no sample clip; pass one as argv[1])\n");
        return 0;
    }

    fs::path scratch = fs::temp_directory_path() / "arca_mediaverify";
    fs::remove_all(scratch);
    fs::create_directories(scratch / "media");

    fs::path media = scratch / "media" / "sample.mkv";
    fs::copy_file(clip, media, fs::copy_options::overwrite_existing);
    touch(scratch / "media" / "ignored.txt");

    arca_db *db = arca_db_open((scratch / "test.db").string().c_str());
    CHECK(db != nullptr, "db open");
    if (!db)
        return 1;

    char *tools = arca_media_tools_status_json(db);
    bool have_tools = contains(tools, "\"ffprobeAvailable\":true") &&
                      contains(tools, "\"ffmpegAvailable\":true");
    arca_string_free(tools);
    if (!have_tools) {
        arca_db_close(db);
        fs::remove_all(scratch);
        std::printf("media-verify: SKIP (ffprobe/ffmpeg unavailable)\n");
        return 0;
    }

    int64_t lib = arca_library_add(db, "Media", (scratch / "media").string().c_str(),
                                   ARCA_LIB_OFFLINE);
    CHECK(lib > 0, "library add");
    int added = 0, removed = 0;
    CHECK(arca_library_scan(db, lib, &added, &removed) == ARCA_OK && added == 1,
          "scan sample");

    char *list = arca_media_list_json(db, lib);
    std::string id = first_id(list);
    CHECK(!id.empty() && contains(list, "sample.mkv"), "sample id discovered");
    arca_string_free(list);

    if (!id.empty()) {
        arca_status probe_status = arca_media_probe(db, id.c_str(), true);
        CHECK(probe_status == ARCA_OK,
              "probe + thumbnails");
        char *detail = arca_media_detail_json(db, id.c_str());
        CHECK(contains(detail, "\"status\":\"ready\"") &&
                  contains(detail, "\"durationSeconds\"") &&
                  contains(detail, "\"videoCodec\"") &&
                  contains(detail, "thumb_0.jpg"),
              "detail includes probe fields and thumbnails");
        arca_string_free(detail);

        int probed = 0, failed = 0;
        CHECK(arca_library_probe_missing(db, lib, 10, &probed, &failed) == ARCA_OK &&
                  probed == 0 && failed == 0,
              "probe-missing ignores fresh rows");
    }

    arca_db_close(db);
    fs::remove_all(scratch);
    std::printf("media-verify: %s (%d failures)\n",
                g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
