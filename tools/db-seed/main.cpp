// db-seed — minimal CLI for the library DB (imports + listings). Useful for
// scripted setup and for exercising the same DB the shell opens.
//
//   db-seed <db> add <name> <root> <offline|online>   (adds + scans)
//   db-seed <db> list
//   db-seed <db> media <library_id>

#include <arca/arca.h>
#include <arca/arca_library.h>

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: db-seed <db> add <name> <root> "
                             "<offline|online> | list | media <id>\n");
        return 2;
    }
    arca_db *db = arca_db_open(argv[1]);
    if (!db) {
        std::fprintf(stderr, "db open failed: %s\n", argv[1]);
        return 1;
    }

    int rc = 0;
    const std::string cmd = argv[2];
    if (cmd == "add" && argc == 6) {
        arca_library_mode mode = std::strcmp(argv[5], "online") == 0
                                     ? ARCA_LIB_ONLINE : ARCA_LIB_OFFLINE;
        int64_t id = arca_library_add(db, argv[3], argv[4], mode);
        if (id < 0) {
            std::fprintf(stderr, "add failed\n");
            rc = 1;
        } else {
            int added = 0, removed = 0;
            arca_library_scan(db, id, &added, &removed);
            std::printf("library %lld: +%d -%d\n", (long long)id, added, removed);
        }
    } else if (cmd == "list") {
        char *json = arca_library_list_json(db);
        std::printf("%s\n", json ? json : "null");
        arca_string_free(json);
    } else if (cmd == "media" && argc == 4) {
        char *json = arca_media_list_json(db, std::atoll(argv[3]));
        std::printf("%s\n", json ? json : "null");
        arca_string_free(json);
    } else {
        std::fprintf(stderr, "bad arguments\n");
        rc = 2;
    }
    arca_db_close(db);
    return rc;
}
