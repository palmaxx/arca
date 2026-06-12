// ONLINE-library local indexing: filename parsing + grouping. Pure local
// computation — this module must never gain network access; it only
// *populates the queue* (`online_media_info`, enrich_status='pending') that
// a future fetcher phase will consume. See arca_library.h boundary note.

#ifndef ARCA_ONLINE_INDEX_H
#define ARCA_ONLINE_INDEX_H

#include <optional>
#include <string>

namespace arca {

struct ParsedName {
    std::string title;            // cleaned display title
    std::optional<int> year;
    std::optional<int> season;
    std::optional<int> episode;
    std::string group_key;        // series share one key; movies stand alone
};

// Parses a media file name (without directories) into title/year/SxxEyy and
// a grouping key. Reference behavior: streamxs filename-parser + Fluss
// FilenameMediaParser (the unit-tested pipelines this ports from).
ParsedName parse_media_filename(const std::string &file_name);

} // namespace arca

#endif // ARCA_ONLINE_INDEX_H
