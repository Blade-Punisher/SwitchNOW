#include "diagnostic_file.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace opennow::diagnostics {
void ArchiveAttempts(const std::string& home, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos ||
            name.find("..") != std::string::npos) continue;
        const auto snapshot = ReadTail(home, name);
        if (snapshot.status != "ok") continue;
        const auto first = home + "/attempt1_" + name;
        const auto second = home + "/attempt2_" + name;
        std::remove(second.c_str());
        (void)std::rename(first.c_str(), second.c_str());
        std::ofstream out(first, std::ios::binary | std::ios::trunc);
        if (out) out << snapshot.content;
    }
}
FileSnapshot ReadTail(const std::string& home, const std::string& name, std::size_t limit) {
    FileSnapshot result;
    result.name = name;
    const auto path = home + "/" + name;
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        result.error = errno;
        result.status = errno == ENOENT ? "missing" : "stat_error";
        return result;
    }
    result.source_bytes = static_cast<std::uint64_t>(info.st_size);
    errno = 0;
    // Do not open with ios::ate: failures must distinguish open/seek/read.
    std::ifstream in(path, std::ios::binary);
    if (!in) { result.status = "open_error"; result.error = errno; return result; }
    const auto count = std::min<std::uint64_t>(result.source_bytes, limit);
    const auto offset = result.source_bytes - count;
    if (offset) {
        in.seekg(static_cast<std::streamoff>(offset));
        if (!in) { result.status = "seek_error"; result.error = errno; return result; }
    }
    result.content.resize(static_cast<std::size_t>(count));
    if (count) in.read(result.content.data(), static_cast<std::streamsize>(count));
    result.content.resize(static_cast<std::size_t>(in.gcount()));
    if (in.bad() || (count && result.content.size() != count)) {
        result.status = "read_error"; result.error = errno; return result;
    }
    result.truncated = offset != 0;
    if (offset) {
        // Never treat a fragment without its original prefix as an event.
        const auto newline = result.content.find('\n');
        result.content.erase(0, newline == std::string::npos ? result.content.size() : newline + 1);
    }
    result.status = result.source_bytes ? "ok" : "empty";
    return result;
}
std::vector<FileSnapshot> CaptureClosedFiles(const std::string& home,
    const std::vector<std::string>& names, const std::function<void()>& close_writer) {
    close_writer(); // Must execute on the thread that owns the buffered files.
    std::vector<FileSnapshot> result;
    for (const auto& name : names) result.push_back(ReadTail(home, name));
    return result;
}
} // namespace opennow::diagnostics
