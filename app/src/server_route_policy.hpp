#pragma once

#include "server_location_policy.hpp"
#include <string>
#include <vector>

namespace opennow::server_route {

struct Observation {
    std::string provider;
    std::string requested;
    std::string actual;
    std::string session_id;
    int rtt_ms = -1;
    int p95_ms = -1;
    int seconds = 0;
    int transport_pauses = 0;
    int transport_pause_ms = 0;
    std::int64_t at_ms = 0;
    bool redirected = false;
    std::string media_endpoint {};
    int playback_pauses = 0, playback_pause_ms = 0;
    int low_display_ms = 0, audio_underruns = 0;
};

struct Estimate {
    int penalty_ms = 0;
    int expected_rtt_ms = -1;
    int samples = 0;
    int redirects = 0;
    std::int64_t newest_ms = 0;
};

inline int Freshness(std::int64_t at, std::int64_t now) {
    if (!server_location::IsProbeFresh(at, now, 7LL * 24 * 3600 * 1000)) return 0;
    return now - at <= 3600000 ? 100 : now - at <= 86400000 ? 50 : 25;
}

inline Estimate Predict(const std::vector<Observation>& history,
    const std::string& provider, const std::string& requested, int tcp_ms,
    std::int64_t now) {
    Estimate result;
    if (tcp_ms < 0 || provider.empty() || requested.empty()) return result;
    std::vector<const Observation*> recent;
    for (const auto& row : history) {
        if (row.provider != provider || row.seconds < 60 || row.rtt_ms < 1 ||
            row.rtt_ms > 300 || row.session_id.empty() || row.actual.empty() ||
            server_location::RouteHost(row.requested) != server_location::RouteHost(requested) ||
            !Freshness(row.at_ms, now)) continue;
        recent.push_back(&row);
    }
    std::stable_sort(recent.begin(), recent.end(), [](const auto* a, const auto* b) {
        return a->at_ms > b->at_ms;
    });
    std::vector<std::string> sessions;
    int weight_sum = 0, rtt_sum = 0, independent = 0;
    std::int64_t quality_sum_x1000 = 0;
    for (const auto* row : recent) {
        // Reconnecting to the same cloud game is one observation, not evidence
        // of several independent allocations. Use at most six recent sessions.
        if (std::find(sessions.begin(), sessions.end(), row->session_id) != sessions.end()) continue;
        sessions.push_back(row->session_id);
        const int weight = Freshness(row->at_ms, now);
        const int tail = row->p95_ms >= row->rtt_ms && row->p95_ms <= 2000
            ? std::clamp((row->p95_ms - row->rtt_ms) / 4, 0, 12) : 0;
        const auto seconds = static_cast<std::int64_t>(row->seconds);
        const auto pauses = std::min<std::int64_t>(12000,
            std::max(0, row->transport_pauses) * 120000LL / seconds);
        const auto stalled = std::min<std::int64_t>(12000,
            std::max(0, row->transport_pause_ms) * 60LL / seconds);
        const auto delivery = std::min<std::int64_t>(12000,
            std::max(0, row->playback_pauses) * 30000LL / seconds +
            std::max(0, row->playback_pause_ms) * 120LL / seconds);
        const auto low_fps = std::min<std::int64_t>(8000,
            std::max(0, row->low_display_ms) * 100LL / seconds);
        const auto audio = std::min<std::int64_t>(4000,
            std::max(0, row->audio_underruns) * 30000LL / seconds);
        weight_sum += weight;
        rtt_sum += row->rtt_ms * weight;
        quality_sum_x1000 += std::min<std::int64_t>(16000,
            tail * 1000LL + std::max(pauses + stalled, delivery) + low_fps + audio) * weight;
        independent += !row->session_id.starts_with("legacy:");
        ++result.samples;
        result.redirects += row->redirected;
        result.newest_ms = std::max(result.newest_ms, row->at_ms);
        if (sessions.size() == 6) break;
    }
    if (!weight_sum) return result;
    result.expected_rtt_ms = (rtt_sum + weight_sum / 2) / weight_sum;
    // One unlucky allocation must never blacklist a good location. Do not use
    // old aggregate counters as multiple independent measurements either.
    const int confidence = std::min(100, weight_sum / 4);
    const int extra = std::max(0, result.expected_rtt_ms - tcp_ms) +
        static_cast<int>((quality_sum_x1000 + weight_sum * 500LL) / (weight_sum * 1000LL));
    // Warm-up observations can break a near tie, but cannot blacklist a route.
    if (!independent) return result; // Old aggregate history has no verified allocation identity.
    const int cap = independent == 1 ? 2 : independent == 2 ? 4 : 24;
    result.penalty_ms = std::min(cap, extra * confidence / 100);
    return result;
}

} // namespace opennow::server_route
