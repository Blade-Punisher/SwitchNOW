#pragma once
#include <functional>
#include <string>
#include <vector>

namespace opennow::gfn::library {
struct ScanResult {
    std::vector<std::string> owned_apps;
    size_t pages = 0, scanned = 0;
};
bool OwnedStatus(const std::string& status);
std::string RequestBody(const std::string& vpc, const std::string& cursor);
// Returns only after reaching the last page. A repeated/missing cursor or
// malformed/failed page is an incomplete refresh, never a complete library.
ScanResult Scan(const std::function<std::string(const std::string&)>& fetch_page,
    const std::function<void(const ScanResult&)>& progress = {});
}
