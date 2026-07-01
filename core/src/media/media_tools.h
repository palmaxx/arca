#ifndef ARCA_MEDIA_TOOLS_H
#define ARCA_MEDIA_TOOLS_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace arca {

struct MediaProbeResult {
    bool ok = false;
    std::string status;
    std::string error;
    double duration_seconds = 0.0;
    int width = 0;
    int height = 0;
    int64_t bitrate = 0;
    std::string frame_rate;
    std::string video_codec;
    std::string audio_codec;
    std::string pixel_format;
    std::string color_space;
    std::string color_transfer;
    std::string color_primaries;
    std::string profile;
    std::string dynamic_range;
};

struct MediaToolStatus {
    std::string ffprobe_path;
    std::string ffmpeg_path;
    bool ffprobe_available = false;
    bool ffmpeg_available = false;
};

MediaToolStatus media_tool_status();
MediaProbeResult probe_media_file(const std::filesystem::path &path);
bool generate_media_thumbnails(const std::filesystem::path &media_path,
                               const std::filesystem::path &cache_dir,
                               double duration_seconds,
                               std::vector<std::filesystem::path> &out_paths,
                               std::string &error);

} // namespace arca

#endif // ARCA_MEDIA_TOOLS_H
