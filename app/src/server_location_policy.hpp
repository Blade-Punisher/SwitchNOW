#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace opennow::server_location
{

struct LatencyEstimate
{
    int median_ms = -1;
    int jitter_ms = -1;
    int selection_score_ms = -1;
    int successful_probes = 0;
};

inline LatencyEstimate EstimateLatency(
    std::vector<int> samples,
    int attempted_probes)
{
    samples.erase(
        std::remove_if(samples.begin(), samples.end(), [](int value) {
            return value < 0;
        }),
        samples.end());
    if (samples.empty())
        return {};

    std::sort(samples.begin(), samples.end());
    const size_t count = samples.size();
    const int median = count % 2 == 0
        ? (samples[count / 2 - 1] + samples[count / 2] + 1) / 2
        : samples[count / 2];

    // Use the inner spread when enough probes succeeded so a single scheduler
    // spike cannot incorrectly disqualify an otherwise nearby region.
    const size_t low_index = count >= 4 ? (count - 1) / 4 : 0;
    const size_t high_index = count >= 4 ? (3 * (count - 1)) / 4 : count - 1;
    const int jitter = samples[high_index] - samples[low_index];
    const int failed = std::max(0, attempted_probes - static_cast<int>(count));
    const int sparse_sample_penalty = count == 1 ? 100 : 0;
    const int stability_penalty = std::min(75, (jitter + 1) / 2);

    return {
        median,
        jitter,
        median + stability_penalty + failed * 20 + sparse_sample_penalty,
        static_cast<int>(count),
    };
}

template <typename Region, typename = void>
struct HasSelectionScore : std::false_type
{
};

inline bool IsProbeFresh(std::int64_t measured_ms, std::int64_t now_ms,
                         std::int64_t lifetime_ms) {
    return measured_ms > 0 && now_ms >= measured_ms &&
        now_ms - measured_ms <= lifetime_ms;
}

// Only known endpoint aliases are mapped. An unknown partner endpoint is not
// evidence that its TCP service and a media node belong to the same location.
inline std::string RouteHost(std::string value) {
    const auto scheme = value.find("://");
    if (scheme != std::string::npos) value.erase(0, scheme + 3);
    value = value.substr(0, value.find_first_of("/?#"));
    if (value.starts_with('[')) {
        const auto end = value.find(']');
        if (end == std::string::npos) return {};
        value.resize(end + 1);
    } else value = value.substr(0, value.find(':'));
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
inline bool HistoryMatchesRegion(const std::string& endpoint, const std::string& actual) {
    const auto requested = RouteHost(endpoint), node = RouteHost(actual);
    if (requested.empty() || node.empty()) return false;
    if (requested == node) return true;
    constexpr const char* suffix = ".cloudmatchbeta.nvidiagrid.net";
    const auto end = requested.find(suffix);
    if (end == std::string::npos || requested.substr(end) != suffix ||
        !node.ends_with(suffix)) return false;
    const auto alias = requested.substr(0, end);
    const char* prefix = alias == "eu-poland" ? "np-waw-" :
        alias == "eu-germany" ? "np-frk-" :
        alias == "eu-france-1" || alias == "eu-france-2" ? "np-par-" :
        alias == "eu-netherlands-1" || alias == "eu-netherlands-2" ||
        alias == "eu-netherlands-north" || alias == "eu-netherlands-south" ? "np-ams-" : "";
    return *prefix && node.rfind(prefix, 0) == 0;
}
template <typename Regions>
size_t ObservedRegionIndex(const Regions& regions, const std::string& requested,
                           const std::string& actual) {
    // The physical node is unambiguous even when multiple country aliases
    // map to it. Update that exact node rather than abandoning the sample.
    const auto actual_host = RouteHost(actual);
    if (!actual_host.empty())
        for (size_t i = 0; i < regions.size(); ++i)
            if (RouteHost(regions[i].url) == actual_host) return i;
    size_t candidate = regions.size(), matches = 0;
    for (size_t i = 0; i < regions.size(); ++i) {
        if (!HistoryMatchesRegion(regions[i].url, actual)) continue;
        if (RouteHost(regions[i].url) == RouteHost(requested)) return i;
        candidate = i; ++matches;
    }
    return matches == 1 ? candidate : regions.size();
}
template <typename Region>
int HistoryWeight(const Region& region, std::int64_t now_ms) {
    if constexpr (requires { region.observed_at_ms; region.observed_session_seconds;
                             region.observed_server_host; region.url; }) {
        if (region.observed_session_seconds < 60 || !IsProbeFresh(
            region.observed_at_ms, now_ms, 7LL * 24 * 3600 * 1000) ||
            !HistoryMatchesRegion(region.url, region.observed_server_host)) return 0;
        const auto age = now_ms - region.observed_at_ms;
        return age <= 3600000 ? 100 : age <= 86400000 ? 50 : 25;
    }
    return 0;
}

template <typename Region, typename = void>
struct HasObservedStreamRtt : std::false_type
{
};

template <typename Region>
struct HasObservedStreamRtt<Region,
    std::void_t<decltype(std::declval<const Region&>().observed_stream_rtt_ms)>>
    : std::true_type
{
};

template <typename Region, typename = void>
struct HasObservedQuality : std::false_type {};

template <typename Region>
struct HasObservedQuality<Region, std::void_t<
    decltype(std::declval<const Region&>().observed_stream_p95_ms),
    decltype(std::declval<const Region&>().observed_transport_pause_count),
    decltype(std::declval<const Region&>().observed_transport_pause_ms),
    decltype(std::declval<const Region&>().observed_session_seconds),
    decltype(std::declval<const Region&>().observed_at_ms)>> : std::true_type {};

template <typename Region>
int ObservedQualityPenalty(const Region& region, std::int64_t now_ms) {
    if constexpr (!HasObservedQuality<Region>::value) return 0;
    else {
        constexpr std::int64_t kFreshnessMs = 7LL * 24 * 60 * 60 * 1000;
        if (region.observed_session_seconds < 60 || region.observed_at_ms <= 0 ||
            now_ms < region.observed_at_ms ||
            now_ms - region.observed_at_ms > kFreshnessMs) return 0;
        const int tail = region.observed_stream_p95_ms > 0 && region.ping_ms >= 0
            ? std::clamp((region.observed_stream_p95_ms - region.ping_ms) / 5, 0, 20) : 0;
        int pause_count = std::max(0, region.observed_transport_pause_count);
        int pause_ms = std::max(0, region.observed_transport_pause_ms);
        // Playback pauses include server pacing, decoder and uncertain events.
        // Retain them for re-probing, but do not charge them to network quality.
        // Keep fractional evidence until the final rounded score. Four real
        // stalls in a 16-minute session must not vanish at each division.
        const std::int64_t seconds = region.observed_session_seconds;
        const auto rate_x1000 = std::min<std::int64_t>(20000,
            static_cast<std::int64_t>(pause_count) * 120000 / seconds);
        const auto duration_x1000 = std::min<std::int64_t>(20000,
            static_cast<std::int64_t>(pause_ms) * 60 / seconds);
        const auto penalty_x1000 = std::min<std::int64_t>(12000,
            tail * 1000LL + rate_x1000 + duration_x1000);
        return static_cast<int>((penalty_x1000 * HistoryWeight(region, now_ms) + 50000) / 100000);
    }
}

template <typename Region>
bool ObservedRouteNeedsProbe(const Region& region, std::int64_t now_ms) {
    if constexpr (requires { region.probe_degraded; })
        if (region.probe_degraded) return true;
    if constexpr (!HasObservedQuality<Region>::value) return false;
    else {
        if (!HistoryWeight(region, now_ms)) return false;
        std::int64_t count = std::max(0, region.observed_transport_pause_count);
        std::int64_t paused_ms = std::max(0, region.observed_transport_pause_ms);
        if constexpr (requires { region.observed_playback_pause_count; region.observed_playback_pause_ms; }) {
            count = std::max<std::int64_t>(count, region.observed_playback_pause_count);
            paused_ms = std::max<std::int64_t>(paused_ms, region.observed_playback_pause_ms);
        }
        // A cached TCP winner is only a hint. Six playback pauses/minute or
        // two percent stalled time require a fresh comparison on next launch.
        return count * 60 >= static_cast<std::int64_t>(region.observed_session_seconds) * 6 ||
            paused_ms * 100 >= static_cast<std::int64_t>(region.observed_session_seconds) * 1000 * 2;
    }
}

template <typename Region>
bool CachedRouteNeedsProbe(const std::vector<Region>& regions,
    const std::string& selected_url, std::int64_t now_ms) {
    for (const auto& region : regions)
        if (region.url == selected_url) return ObservedRouteNeedsProbe(region, now_ms);
    return true; // a route missing from the measured list is not a verified winner
}

template <typename Region>
struct HasSelectionScore<
    Region,
    std::void_t<decltype(std::declval<const Region&>().selection_score_ms)>>
    : std::true_type
{
};

template <typename Region>
int RegionSelectionScore(const Region& region, std::int64_t now_ms =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count())
{
    int score = region.ping_ms;
    if constexpr (HasSelectionScore<Region>::value)
    {
        if (region.selection_score_ms >= 0)
            score = region.selection_score_ms;
    }
    // TCP setup is only a proxy for the media route. Retain a bounded part of
    // the observed in-stream RTT when ranking a region on the next probe.
    if constexpr (HasObservedStreamRtt<Region>::value)
    {
        if (region.observed_stream_rtt_ms > 0 && score >= 0)
            score += std::clamp((region.observed_stream_rtt_ms - region.ping_ms) / 3,
                                0, 12) * HistoryWeight(region, now_ms) / 100;
    }
    if (score >= 0) score += ObservedQualityPenalty(region, now_ms);
    if constexpr (requires { region.route_history_penalty_ms; region.route_history_at_ms; }) {
        if (score >= 0 && IsProbeFresh(region.route_history_at_ms, now_ms, 7LL * 24 * 3600 * 1000)) {
            int base = region.ping_ms;
            if constexpr (HasSelectionScore<Region>::value)
                if (region.selection_score_ms >= 0) base = region.selection_score_ms;
            // Both histories describe the same network: take the stronger
            // evidence rather than charging twice for one bad route.
            score = std::max(score, base + std::clamp(region.route_history_penalty_ms, 0, 24));
        }
    }
    return score;
}

inline bool IsAutomatic(const std::string& value)
{
    return value.empty() || value == "Auto" || value == "Closest";
}

inline bool IsClosest(const std::string& value) { return value == "Closest"; }
inline const char* SelectionMode(const std::string& value) {
    return IsClosest(value) ? "Closest" : IsAutomatic(value) ? "Auto" : "Manual";
}

inline bool IsValidStreamingBaseUrl(const std::string& value)
{
    static constexpr const char* kHttpsPrefix = "https://";
    if (value.rfind(kHttpsPrefix, 0) != 0 || value.size() <= 8)
        return false;

    const size_t authority_end = value.find('/', 8);
    const std::string authority = value.substr(8, authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos)
        return false;

    return std::none_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) || ch < 0x20 || ch == 0x7f;
    });
}

inline std::string NormalizeStreamingBaseUrl(std::string value)
{
    if (!IsValidStreamingBaseUrl(value))
        return {};
    if (value.back() != '/')
        value.push_back('/');
    return value;
}

template <typename RegionRange>
std::string SelectBestMeasuredRegionUrl(const RegionRange& regions)
{
    int best_score_ms = std::numeric_limits<int>::max();
    std::string best_url;
    for (const auto& region : regions)
    {
        const int score_ms = RegionSelectionScore(region);
        if (region.ping_ms < 0 || score_ms < 0 || score_ms >= best_score_ms)
            continue;

        const std::string normalized = NormalizeStreamingBaseUrl(region.url);
        if (normalized.empty())
            continue;

        best_score_ms = score_ms;
        best_url = normalized;
    }
    return best_url;
}

template <typename RegionRange>
std::vector<size_t> SelectLatencyRetries(const RegionRange& regions)
{
    std::vector<size_t> retry;
    for (size_t index = 0; index < regions.size(); ++index)
        if (regions[index].ping_ms < 0 && IsValidStreamingBaseUrl(regions[index].url))
            retry.push_back(index);
    return retry;
}

template <typename RegionRange>
std::vector<size_t> SelectLatencyFinalists(
    const RegionRange& regions,
    size_t maximum_finalists,
    int margin_ms)
{
    std::vector<size_t> ranked;
    ranked.reserve(regions.size());
    for (size_t index = 0; index < regions.size(); ++index)
    {
        if (regions[index].ping_ms >= 0 &&
            !NormalizeStreamingBaseUrl(regions[index].url).empty())
        {
            ranked.push_back(index);
        }
    }

    std::stable_sort(
        ranked.begin(), ranked.end(), [&regions](size_t left, size_t right) {
            if (regions[left].ping_ms != regions[right].ping_ms)
                return regions[left].ping_ms < regions[right].ping_ms;
            return regions[left].name < regions[right].name;
        });
    if (ranked.empty() || maximum_finalists == 0)
        return {};

    const int cutoff = regions[ranked.front()].ping_ms + std::max(0, margin_ms);
    const auto end = std::find_if(
        ranked.begin(), ranked.end(), [&regions, cutoff](size_t index) {
            return regions[index].ping_ms > cutoff;
        });
    ranked.erase(end, ranked.end());
    if (ranked.size() > maximum_finalists)
        ranked.resize(maximum_finalists);
    return ranked;
}

inline std::string ResolveStreamingBaseUrl(
    const std::string& selected_region,
    const std::string& provider_url,
    const std::string& automatic_region_url = {})
{
    if (!IsAutomatic(selected_region))
    {
        const std::string selected = NormalizeStreamingBaseUrl(selected_region);
        if (!selected.empty())
            return selected;
    }
    else
    {
        const std::string automatic =
            NormalizeStreamingBaseUrl(automatic_region_url);
        if (!automatic.empty())
            return automatic;
    }

    return NormalizeStreamingBaseUrl(provider_url);
}

} // namespace opennow::server_location
