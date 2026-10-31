#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace opennow::media_input
{

inline bool HasScheme(std::string_view value)
{
    const auto separator = value.find(':');
    if (separator == std::string_view::npos || separator == 0)
        return false;

    for (size_t index = 0; index < separator; ++index)
    {
        const char ch = value[index];
        const bool valid = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (index > 0 && ch >= '0' && ch <= '9') ||
                           (index > 0 && (ch == '+' || ch == '-' || ch == '.'));
        if (!valid)
            return false;
    }
    return true;
}

inline bool DownloadAdvertisementFile(std::string_view url, std::string_view profile)
{
    std::string address(url);
    std::string kind(profile);
    const auto lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::transform(address.begin(), address.end(), address.begin(), lower);
    std::transform(kind.begin(), kind.end(), kind.begin(), lower);
    if (address.rfind("https://", 0) != 0 && address.rfind("http://", 0) != 0)
        return false;
    // Signed CDN paths often have no extension and use the profile "default".
    // Keep explicit playlists on their existing streaming path; never treat
    // an HLS manifest as a standalone video file.
    const auto end = address.find_first_of("?#");
    const std::string path = address.substr(0, end);
    const bool playlist = kind.find("hls") != std::string::npos ||
        path.find(".m3u8") != std::string::npos || path.find(".mpd") != std::string::npos ||
        kind.find("dash") != std::string::npos;
    return !playlist;
}

inline std::string FfmpegLocalFileUrl(std::string_view path)
{
    if (path.rfind("file:", 0) == 0)
        return std::string(path);

    // libnx mount paths contain a colon (sdmc:/...), which FFmpeg otherwise
    // interprets as an unsupported network protocol named "sdmc".
    return "file:" + std::string(path);
}

} // namespace opennow::media_input
