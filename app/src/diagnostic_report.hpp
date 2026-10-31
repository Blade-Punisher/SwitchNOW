#pragma once
#include <string>
#include <vector>
#include <utility>
#include "diagnostic_file.hpp"

namespace opennow::report {
// Only allowlisted technical events are exported. Raw protocol JSON, login
// files and keyboard/mouse reports are intentionally not copied.
std::string FilterLog(const std::string& text);
void WriteZip(const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& files);
std::string Create(const std::string& home, const std::string& settings_summary,
    const std::vector<diagnostics::FileSnapshot>& captured = {});
void SaveSessionSummary(const std::string& home, const std::string& summary);
void SavePerformance(const std::string& home, const std::string& timeline, const std::string& incidents);
} // namespace opennow::report
