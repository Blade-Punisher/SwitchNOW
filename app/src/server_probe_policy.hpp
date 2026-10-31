#pragma once
#include "models.hpp"
#include "server_location_policy.hpp"

namespace opennow::server_probe {
// Fresh provider routes get broad coverage. Optional direct zones need recent
// observed streaming evidence; queue size alone must not trigger a global scan.
inline std::vector<size_t> CoarseCandidates(const std::vector<StreamRegion>& regions, std::int64_t now) {
    std::vector<size_t> regular, zones;
    for (size_t i = 0; i < regions.size(); ++i) {
        if (regions[i].zone_id.empty()) regular.push_back(i);
        else if (regions[i].observed_stream_rtt_ms > 0 &&
            server_location::IsProbeFresh(regions[i].observed_at_ms, now, 86400000)) zones.push_back(i);
    }
    auto observed = [&](size_t i) {
        return regions[i].observed_stream_rtt_ms > 0 &&
            server_location::IsProbeFresh(regions[i].observed_at_ms, now, 86400000)
            ? regions[i].observed_stream_rtt_ms : 100000;
    };
    std::stable_sort(regular.begin(), regular.end(), [&](size_t a, size_t b) { return observed(a) < observed(b); });
    std::stable_sort(zones.begin(), zones.end(), [&](size_t a, size_t b) { return observed(a) < observed(b); });
    if (regular.size() > 24) {
        std::vector<size_t> bounded;
        for (size_t i = 0; i < 24; ++i) bounded.push_back(regular[i * (regular.size()-1) / 23]);
        regular = std::move(bounded);
    }
    if (zones.size() > 2) zones.resize(2);
    regular.insert(regular.end(), zones.begin(), zones.end());
    return regular;
}
inline bool CommonFailure(unsigned tested, unsigned failed) {
    return tested >= 4 && failed * 4 >= tested * 3;
}
inline std::string TcpEndpoint(const std::string& url) {
    const auto normalized = server_location::NormalizeStreamingBaseUrl(url);
    if (normalized.empty()) return {};
    const bool secure = normalized.starts_with("https://");
    const auto begin = normalized.find("://") + 3;
    auto authority = normalized.substr(begin, normalized.find('/', begin) - begin);
    const auto end_bracket = authority.find(']');
    const bool port = authority.starts_with('[')
        ? end_bracket != std::string::npos && end_bracket + 1 < authority.size()
        : authority.find(':') != std::string::npos;
    if (authority.empty() || !std::all_of(authority.begin(), authority.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '[' || c == ']';
    })) return {};
    // CONNECT_ONLY sends no HTTP and no credentials. Avoid doing an entire
    // TLS handshake just to measure TCP, and avoid TLS socket churn on Switch.
    if (!port) authority += secure ? ":443" : ":80";
    return "http://" + authority + "/";
}
inline StreamRegion Finish(const StreamRegion& coarse, const std::vector<int>& refined,
                           int attempted, bool common_failure) {
    auto result = coarse;
    const auto estimate = server_location::EstimateLatency(refined, attempted);
    result.probe_degraded = false;
    if (estimate.successful_probes >= 2 || coarse.ping_ms < 0) {
        result.ping_ms = estimate.median_ms; result.jitter_ms = estimate.jitter_ms;
        result.selection_score_ms = estimate.selection_score_ms;
        result.successful_probes = estimate.successful_probes;
        result.attempted_probes = attempted;
    } else if (!refined.empty()) {
        auto combined = refined;
        combined.push_back(coarse.ping_ms);
        const auto partial = server_location::EstimateLatency(combined, attempted + coarse.attempted_probes);
        result.ping_ms = partial.median_ms; result.jitter_ms = partial.jitter_ms;
        result.successful_probes = partial.successful_probes;
        result.attempted_probes += attempted;
        result.selection_score_ms = partial.selection_score_ms + 40;
        result.probe_degraded = true;
    } else {
        // Preserve evidence on shared failure or an exhausted sweep deadline.
        // An unattempted finalist is not a failed endpoint.
        result.probe_degraded = common_failure || attempted == 0;
        result.attempted_probes += attempted;
        result.selection_score_ms = std::max(result.selection_score_ms, result.ping_ms + 100) + 40;
        if (!result.probe_degraded) { result.ping_ms = -1; result.selection_score_ms = -1; }
    }
    return result;
}
} // namespace opennow::server_probe
