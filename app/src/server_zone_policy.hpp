#pragma once
#include "models.hpp"
#include "server_location_policy.hpp"
#include <charconv>

namespace opennow::server_zone {
inline bool IsOfficialProvider(const std::string& endpoint) {
    const auto host = server_location::RouteHost(endpoint);
    return host.ends_with(".nvidiagrid.net");
}
inline bool IsZoneId(const std::string& id) {
    if (!id.starts_with("NP-") || id.size() > 20) return false;
    const auto dash = id.find('-', 3);
    if (dash < 5 || dash > 11 || dash == std::string::npos || id.size() - dash - 1 < 2 ||
        id.size() - dash - 1 > 3) return false;
    for (size_t i = 3; i < id.size(); ++i) {
        if (i == dash) continue;
        if (i < dash ? !((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))
                     : !(id[i] >= '0' && id[i] <= '9')) return false;
    }
    return true;
}
inline std::string ZoneUrl(const std::string& id) {
    if (!IsZoneId(id)) return {};
    std::string host = id;
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    return "https://" + host + ".cloudmatchbeta.nvidiagrid.net/";
}
inline bool FreshQueue(const StreamRegion& r, std::int64_t now) {
    return r.zone_queue_estimate >= 0 && server_location::IsProbeFresh(r.zone_queue_at_ms, now, 600000);
}
inline bool FreshZone(const StreamRegion& r, std::int64_t now) {
    return !r.zone_id.empty() && ZoneUrl(r.zone_id) == r.url &&
        server_location::IsProbeFresh(r.zone_catalog_at_ms, now, 120000);
}
inline bool Refined(const StreamRegion& r) {
    return !r.probe_degraded && r.successful_probes >= 2 && r.attempted_probes >= 2;
}
inline std::vector<size_t> SelectionPool(const std::vector<StreamRegion>& regions,
    std::int64_t now) {
    std::vector<size_t> healthy, fallback;
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        if (r.ping_ms < 0 || !server_location::IsValidStreamingBaseUrl(r.url)) continue;
        // Stale/foreign catalogue nodes must never displace a provider region.
        if (!r.zone_id.empty() && !FreshZone(r, now)) continue;
        if (r.zone_id.empty() || r.probe_degraded) fallback.push_back(i);
        if (Refined(r)) healthy.push_back(i);
    }
    // Compare both endpoint classes. A single refined zone is not evidence
    // that every faster regional endpoint has become unsuitable.
    return healthy.empty() ? fallback : healthy;
}
inline std::vector<size_t> AutoPool(const std::vector<StreamRegion>& regions, std::int64_t now) {
    auto pool = SelectionPool(regions, now);
    int best = std::numeric_limits<int>::max();
    for (auto i : pool)
        best = std::min(best, server_location::RegionSelectionScore(regions[i], now));
    // Apply the latency guard to the combined media/history estimate over the
    // entire eligible pool, not just the one available catalogue node.
    std::erase_if(pool, [&](size_t i) {
        const int score = server_location::RegionSelectionScore(regions[i], now);
        return score < 0 || score > best + 15;
    });
    return pool;
}
inline std::vector<int> AutoRanks(const std::vector<StreamRegion>& regions, std::int64_t now) {
    const auto pool = AutoPool(regions, now);
    std::vector<int> ranks(regions.size(), -1);
    int max_score = 1, max_queue = 1;
    bool queue_available = !pool.empty();
    for (auto i : pool) {
        max_score = std::max(max_score, server_location::RegionSelectionScore(regions[i], now));
        max_queue = std::max(max_queue, regions[i].zone_queue_estimate);
        queue_available = queue_available && !regions[i].zone_id.empty() && FreshQueue(regions[i], now);
    }
    // Missing queue estimates (including country endpoints) disable the queue
    // term for the whole comparison. Unknown queue never means empty queue.
    for (auto i : pool) {
        const int score = server_location::RegionSelectionScore(regions[i], now);
        ranks[i] = queue_available ? static_cast<int>(7500LL * score / max_score +
            2500LL * regions[i].zone_queue_estimate / max_queue) : score;
    }
    return ranks;
}
inline int AutoRank(const std::vector<StreamRegion>& regions, size_t index, std::int64_t now) {
    const auto ranks = AutoRanks(regions, now);
    return index < ranks.size() ? ranks[index] : -1;
}
inline std::string Select(const std::vector<StreamRegion>& regions, const std::string& mode,
    std::int64_t now) {
    auto pool = server_location::IsClosest(mode) ? SelectionPool(regions, now) : AutoPool(regions, now);
    const auto ranks = AutoRanks(regions, now);
    int best_score = std::numeric_limits<int>::max();
    std::string best;
    for (auto i : pool) {
        const auto& r = regions[i];
        const int score = server_location::IsClosest(mode) ? r.ping_ms : ranks[i];
        if (score < 0 || score > best_score) continue;
        if (score == best_score && !best.empty() && r.url >= best) continue;
        best_score = score; best = server_location::NormalizeStreamingBaseUrl(r.url);
    }
    return best;
}
inline bool IsCloudMatchHost(const std::string& endpoint) {
    const auto host = server_location::RouteHost(endpoint);
    return host.ends_with(".cloudmatchbeta.nvidiagrid.net") || host.ends_with(".cloudmatch.nvidiagrid.net");
}
inline bool IsDirectControlHost(const std::string& host) {
    if (host.empty() || host != server_location::RouteHost(host) ||
        host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos) return false;
    for (const std::string suffix : {".cloudmatchbeta.nvidiagrid.net", ".cloudmatch.nvidiagrid.net"}) {
        if (!host.ends_with(suffix)) continue;
        auto encoded = host.substr(0, host.size() - suffix.size());
        std::replace(encoded.begin(), encoded.end(), '-', '.');
        // Only public IPv4-shaped rig names; never regional np-/eu- aliases.
        return encoded.find_first_not_of("0123456789.") == std::string::npos &&
            IsDirectControlHost(encoded);
    }
    if (host.ends_with(".nvidiagrid.net")) return true;
    int octets[4] {}; size_t begin = 0;
    for (int i = 0; i < 4; ++i) {
        const auto end = host.find('.', begin);
        if ((i < 3) == (end == std::string::npos)) return false;
        const auto last = end == std::string::npos ? host.size() : end;
        if (last - begin > 1 && host[begin] == '0') return false;
        const auto parsed = std::from_chars(host.data() + begin, host.data() + last, octets[i]);
        if (parsed.ec != std::errc() || parsed.ptr != host.data() + last || octets[i] < 0 || octets[i] > 255) return false;
        begin = last + 1;
    }
    return octets[0] > 0 && octets[0] < 224 && octets[0] != 10 && octets[0] != 127 &&
        !(octets[0] == 169 && octets[1] == 254) && !(octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) &&
        !(octets[0] == 192 && octets[1] == 168) && !(octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127);
}
inline std::string ControlHost(const SessionInfo& info) {
    return info.control_ip.empty() ? info.server_ip : info.control_ip;
}
inline bool ShouldDirectPoll(const std::string& provider, const std::string& base, const SessionInfo& info) {
    return IsOfficialProvider(provider) && IsCloudMatchHost(base) &&
        (info.status == 2 || info.status == 3) && !info.session_id.empty() && IsDirectControlHost(ControlHost(info)) &&
        server_location::RouteHost(base) != ControlHost(info);
}
inline bool AcceptDirectPoll(const SessionInfo& original, const SessionInfo& candidate) {
    return !original.session_id.empty() && candidate.session_id == original.session_id &&
        (candidate.status == 2 || candidate.status == 3) && candidate.signaling_url.starts_with("wss://") &&
        candidate.signaling_url.size() > 6 && candidate.signaling_url.find_first_of(" \t\r\n@\\") == std::string::npos &&
        !candidate.media_ip.empty() && candidate.media_port > 0 && candidate.media_port <= 65535;
}
} // namespace opennow::server_zone
