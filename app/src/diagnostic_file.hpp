#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace opennow::diagnostics {
struct FileSnapshot {
    std::string name, status = "missing", content;
    std::uint64_t source_bytes = 0;
    int error = 0;
    bool truncated = false;
};
FileSnapshot ReadTail(const std::string& home, const std::string& name,
                      std::size_t limit = 512 * 1024);
void ArchiveAttempts(const std::string& home, const std::vector<std::string>& names);
std::vector<FileSnapshot> CaptureClosedFiles(const std::string& home,
    const std::vector<std::string>& names, const std::function<void()>& close_writer);
} // namespace opennow::diagnostics
