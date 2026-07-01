#include "media_tools.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace arca {
namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return c > ' '; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lower_ascii(std::string value) {
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

fs::path path_from_utf8(const std::string &s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string utf8_of(const fs::path &p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string env_value(const char *name) {
#ifdef _WIN32
    char *value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
        return "";
    std::string out(value);
    std::free(value);
    return out;
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}

fs::path executable_dir() {
#ifdef _WIN32
    std::array<char, MAX_PATH> buf{};
    DWORD n = GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n > 0 && n < buf.size())
        return fs::path(buf.data()).parent_path();
#endif
    return fs::current_path();
}

std::string quote_arg(const std::string &arg) {
    std::string out = "\"";
    for (char c : arg) {
        if (c == '"')
            out += "\\\"";
        else
            out += c;
    }
    out += '"';
    return out;
}

std::string executable_name(const char *base) {
#ifdef _WIN32
    return std::string(base) + ".exe";
#else
    return base;
#endif
}

std::string resolve_tool(const char *env_name, const char *base_name) {
    std::string from_env = env_value(env_name);
    if (!from_env.empty())
        return from_env;

    std::string exe_name = executable_name(base_name);
    std::vector<fs::path> candidates;
    fs::path exe_dir = executable_dir();
    candidates.push_back(exe_dir / exe_name);
    candidates.push_back(exe_dir / "assets" / "ffmpeg" / "bin" / exe_name);
    candidates.push_back(fs::current_path() / "assets" / "ffmpeg" / "bin" / exe_name);
    candidates.push_back(fs::current_path() / exe_name);
    for (const auto &candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
            return utf8_of(candidate);
    }
#ifdef _WIN32
    std::array<char, MAX_PATH> resolved{};
    DWORD found = SearchPathA(nullptr, exe_name.c_str(), nullptr,
                              static_cast<DWORD>(resolved.size()),
                              resolved.data(), nullptr);
    if (found > 0 && found < resolved.size())
        return std::string(resolved.data());
#endif
    return exe_name;
}

bool run_capture(const std::string &exe, const std::string &args,
                 std::string &output, int *exit_code = nullptr) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit_handles{};
    inherit_handles.nLength = sizeof(inherit_handles);
    inherit_handles.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &inherit_handles, 0)) {
        output = "could not create process pipe: " + std::to_string(GetLastError());
        if (exit_code)
            *exit_code = -1;
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul_input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &inherit_handles, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                   nullptr);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nul_input != INVALID_HANDLE_VALUE ? nul_input : nullptr;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;

    PROCESS_INFORMATION process{};
    std::string command = quote_arg(exe);
    if (!args.empty())
        command += " " + args;
    std::vector<char> command_line(command.begin(), command.end());
    command_line.push_back('\0');

    BOOL started = CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    DWORD start_error = started ? ERROR_SUCCESS : GetLastError();
    CloseHandle(write_pipe);
    if (nul_input != INVALID_HANDLE_VALUE)
        CloseHandle(nul_input);
    if (!started) {
        CloseHandle(read_pipe);
        output = "could not start process: " + command + " (" +
                 std::to_string(start_error) + ")";
        if (exit_code)
            *exit_code = -1;
        return false;
    }

    std::array<char, 4096> buffer{};
    output.clear();
    for (;;) {
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                           &bytes_read, nullptr);
        if (!ok || bytes_read == 0)
            break;
        output.append(buffer.data(), bytes_read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(process.hProcess, &rc);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code)
        *exit_code = static_cast<int>(rc);
    return rc == 0;
#else
    std::string command = quote_arg(exe) + " " + args + " 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        output = "could not start process: " + command;
        if (exit_code)
            *exit_code = -1;
        return false;
    }

    std::array<char, 4096> buffer{};
    output.clear();
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
        output += buffer.data();

    int rc = pclose(pipe);
    if (exit_code)
        *exit_code = rc;
    return rc == 0;
#endif
}

bool tool_available(const std::string &exe) {
    std::string output;
    return run_capture(exe, "-version", output);
}

std::unordered_map<std::string, std::string> parse_key_values(const std::string &output) {
    std::unordered_map<std::string, std::string> values;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = trim(line.substr(eq + 1));
        if (value.empty() || value == "N/A")
            continue;
        if (!values.count(key))
            values.emplace(std::move(key), std::move(value));
    }
    return values;
}

double parse_double(const std::unordered_map<std::string, std::string> &m,
                    const char *key) {
    auto it = m.find(key);
    if (it == m.end())
        return 0.0;
    char *end = nullptr;
    double v = std::strtod(it->second.c_str(), &end);
    return end && end != it->second.c_str() ? v : 0.0;
}

int parse_int(const std::unordered_map<std::string, std::string> &m,
              const char *key) {
    auto it = m.find(key);
    if (it == m.end())
        return 0;
    char *end = nullptr;
    long v = std::strtol(it->second.c_str(), &end, 10);
    return end && end != it->second.c_str() ? static_cast<int>(v) : 0;
}

int64_t parse_i64(const std::unordered_map<std::string, std::string> &m,
                  const char *key) {
    auto it = m.find(key);
    if (it == m.end())
        return 0;
    char *end = nullptr;
    long long v = std::strtoll(it->second.c_str(), &end, 10);
    return end && end != it->second.c_str() ? static_cast<int64_t>(v) : 0;
}

std::string map_get(const std::unordered_map<std::string, std::string> &m,
                    const char *key) {
    auto it = m.find(key);
    return it == m.end() ? std::string() : it->second;
}

std::string format_rate(const std::string &rate) {
    size_t slash = rate.find('/');
    if (slash == std::string::npos)
        return rate;
    double num = std::strtod(rate.substr(0, slash).c_str(), nullptr);
    double den = std::strtod(rate.substr(slash + 1).c_str(), nullptr);
    if (num <= 0 || den <= 0)
        return "";
    double fps = num / den;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g fps", fps);
    return buf;
}

std::string dynamic_range_from(const MediaProbeResult &r) {
    std::string transfer = lower_ascii(r.color_transfer);
    std::string primaries = lower_ascii(r.color_primaries);
    std::string space = lower_ascii(r.color_space);
    std::string pix = lower_ascii(r.pixel_format);
    if (transfer.find("smpte2084") != std::string::npos ||
        transfer.find("arib-std-b67") != std::string::npos ||
        transfer.find("pq") != std::string::npos)
        return "HDR";
    if (primaries.find("bt2020") != std::string::npos ||
        space.find("bt2020") != std::string::npos)
        return "HDR";
    if (!pix.empty() || !transfer.empty() || !primaries.empty())
        return "SDR";
    return "";
}

std::string seek_arg(double seconds) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", (std::max)(0.0, seconds));
    return buf;
}

} // namespace

MediaToolStatus media_tool_status() {
    MediaToolStatus status;
    status.ffprobe_path = resolve_tool("ARCA_FFPROBE", "ffprobe");
    status.ffmpeg_path = resolve_tool("ARCA_FFMPEG", "ffmpeg");
    status.ffprobe_available = tool_available(status.ffprobe_path);
    status.ffmpeg_available = tool_available(status.ffmpeg_path);
    return status;
}

MediaProbeResult probe_media_file(const fs::path &path) {
    MediaProbeResult result;
    MediaToolStatus tools = media_tool_status();
    if (!tools.ffprobe_available) {
        result.status = "missing-tools";
        result.error = "ffprobe not found";
        return result;
    }

    std::string output;
    std::string args =
        "-v error -select_streams v:0 "
        "-show_entries stream=codec_name,profile,width,height,avg_frame_rate,"
        "pix_fmt,color_space,color_transfer,color_primaries,bit_rate "
        "-show_entries format=duration,bit_rate "
        "-of default=noprint_wrappers=1:nokey=0 " +
        quote_arg(utf8_of(path));
    if (!run_capture(tools.ffprobe_path, args, output)) {
        result.status = "failed";
        result.error = trim(output);
        if (result.error.empty())
            result.error = "ffprobe failed";
        return result;
    }

    auto values = parse_key_values(output);
    result.duration_seconds = parse_double(values, "duration");
    result.width = parse_int(values, "width");
    result.height = parse_int(values, "height");
    result.bitrate = parse_i64(values, "bit_rate");
    result.frame_rate = format_rate(map_get(values, "avg_frame_rate"));
    result.video_codec = map_get(values, "codec_name");
    result.pixel_format = map_get(values, "pix_fmt");
    result.color_space = map_get(values, "color_space");
    result.color_transfer = map_get(values, "color_transfer");
    result.color_primaries = map_get(values, "color_primaries");
    result.profile = map_get(values, "profile");

    output.clear();
    args = "-v error -select_streams a:0 "
           "-show_entries stream=codec_name "
           "-of default=noprint_wrappers=1:nokey=0 " +
           quote_arg(utf8_of(path));
    if (run_capture(tools.ffprobe_path, args, output)) {
        auto audio = parse_key_values(output);
        result.audio_codec = map_get(audio, "codec_name");
    }

    result.dynamic_range = dynamic_range_from(result);
    result.status = "ready";
    result.ok = true;
    return result;
}

bool generate_media_thumbnails(const fs::path &media_path, const fs::path &cache_dir,
                               double duration_seconds,
                               std::vector<fs::path> &out_paths,
                               std::string &error) {
    MediaToolStatus tools = media_tool_status();
    if (!tools.ffmpeg_available) {
        error = "ffmpeg not found";
        return false;
    }

    std::error_code ec;
    fs::create_directories(cache_dir, ec);
    if (ec) {
        error = "could not create thumbnail cache";
        return false;
    }

    double duration = (std::max)(0.0, duration_seconds);
    std::array<double, 3> seeks{};
    if (duration > 30.0) {
        seeks = {duration * 0.10, duration * 0.50, duration * 0.90};
    } else if (duration > 3.0) {
        seeks = {1.0, duration * 0.50, (std::max)(1.0, duration - 1.0)};
    } else {
        seeks = {0.0, 0.0, 0.0};
    }

    out_paths.clear();
    bool any = false;
    for (size_t i = 0; i < seeks.size(); ++i) {
        fs::path out = cache_dir / ("thumb_" + std::to_string(i) + ".jpg");
        std::string output;
        std::string args =
            "-y -ss " + seek_arg(seeks[i]) + " -i " + quote_arg(utf8_of(media_path)) +
            " -frames:v 1 -vf " + quote_arg("scale=320:-1:force_original_aspect_ratio=decrease") +
            " -q:v 3 " + quote_arg(utf8_of(out));
        if (run_capture(tools.ffmpeg_path, args, output)) {
            std::error_code exists_ec;
            if (fs::is_regular_file(out, exists_ec)) {
                out_paths.push_back(out);
                any = true;
            }
        } else if (error.empty()) {
            error = trim(output);
        }
    }
    if (!any && error.empty())
        error = "ffmpeg did not generate thumbnails";
    return any;
}

} // namespace arca
