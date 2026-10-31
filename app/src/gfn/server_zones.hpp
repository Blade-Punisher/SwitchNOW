#pragma once
#include "../models.hpp"
#include <string>
#include <vector>
namespace opennow::gfn {
struct ZoneCatalogDiagnostics {
    unsigned fresh = 0, stale = 0, future = 0, invalid = 0;
    std::int64_t newest_age_ms = -1;
};
std::vector<StreamRegion> ParseServerZones(const std::string& queue_json,
    const std::string& mapping_json, std::int64_t now_ms, ZoneCatalogDiagnostics* diagnostics = nullptr);
} // namespace opennow::gfn
