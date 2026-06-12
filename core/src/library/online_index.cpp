#include "online_index.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace arca {

namespace {

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string collapse_spaces(const std::string &in) {
    std::string out;
    bool space = false;
    for (char c : in) {
        if (c == ' ') {
            space = true;
        } else {
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += c;
        }
    }
    return out;
}

// Tokens after which release-name noise typically starts; truncating at the
// first hit cleans titles like "Name 2006 2160p DV HDR x265-GRP".
const std::regex kNoise(
    R"((?:^|\s)(2160p|1080p|720p|480p|4k|uhd|bluray|blu-ray|bdremux|remux|web-?dl|webrip|hdtv|x26[45]|h26[45]|hevc|avc|aac|ac3|eac3|dts(?:-hd)?|truehd|atmos|dv|dovi|hdr10\+?|hdr|sdr|10bit|8bit|hybrid|proper|repack|extended|remastered|imax)(?:\s|$))",
    std::regex::icase);

const std::regex kSeasonEpisode(R"([Ss](\d{1,2})[\s.._-]*[Ee](\d{1,3}))");
const std::regex kYear(R"((?:^|[\s(\[])((?:19|20)\d{2})(?:[\s)\]]|$))");

} // namespace

ParsedName parse_media_filename(const std::string &file_name) {
    // Strip extension.
    std::string base = file_name;
    if (auto dot = base.find_last_of('.'); dot != std::string::npos && dot > 0)
        base = base.substr(0, dot);

    // Separators to spaces.
    for (char &c : base)
        if (c == '.' || c == '_')
            c = ' ';
    base = collapse_spaces(base);

    ParsedName out;
    size_t title_end = base.size();

    std::smatch m;
    if (std::regex_search(base, m, kSeasonEpisode)) {
        out.season = std::stoi(m[1].str());
        out.episode = std::stoi(m[2].str());
        title_end = std::min(title_end, (size_t)m.position(0));
    }
    if (std::regex_search(base, m, kYear)) {
        out.year = std::stoi(m[1].str());
        // Year usually terminates the title (unless it *is* the title).
        if (m.position(1) > 0)
            title_end = std::min(title_end, (size_t)m.position(0));
    }
    if (std::regex_search(base, m, kNoise))
        title_end = std::min(title_end, (size_t)m.position(0));

    std::string title = collapse_spaces(base.substr(0, title_end));
    // Trim stray brackets/dashes.
    while (!title.empty() && (title.back() == '-' || title.back() == '(' ||
                              title.back() == '[' || title.back() == ' '))
        title.pop_back();
    if (title.empty())
        title = base;
    out.title = title;

    if (out.season.has_value()) {
        out.group_key = lowercase(title);  // series group across episodes
    } else {
        out.group_key = lowercase(title);
        if (out.year)
            out.group_key += " (" + std::to_string(*out.year) + ")";
    }
    return out;
}

} // namespace arca
