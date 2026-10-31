#pragma once
#include "models.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace opennow::gfn::feed {
struct CachedFeed {
    std::vector<GameInfo> games;
    std::int64_t scanned_at_ms = 0, updated_at_ms = 0;
};
std::string CachePath(const std::string& directory, const std::string& owner);
std::optional<CachedFeed> LoadCache(const std::string& directory, const std::string& owner);
// Only complete feed snapshots enter the persistent cache. Tokens,
// credentials, HTTP responses and panel-only results are never serialized.
bool SaveCache(const std::string& directory, const std::string& owner, const CachedFeed& cache, std::optional<std::uint64_t> revision = {});
void InvalidateAccount(const std::string& directory,const std::string& identity);
}
