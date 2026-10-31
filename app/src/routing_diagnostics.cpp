#include "routing_diagnostics.hpp"
#include "server_location_policy.hpp"
#include "server_zone_policy.hpp"
#include "diagnostic_file.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>

namespace opennow::routing {
namespace {
std::mutex mutex;
std::string Atom(std::string value) {
    if (value.size() > 180) value.resize(180);
    for (char& ch : value)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) ||
              std::string("._-:[];,/").find(ch) != std::string::npos)) ch = '_';
    return value;
}
std::int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}
std::string Host(std::string value) {
    const auto scheme = value.find("://");
    if (scheme != std::string::npos) value.erase(0, scheme + 3);
    value = value.substr(0, value.find_first_of("/?#"));
    const auto at = value.rfind('@');
    if (at != std::string::npos) value.erase(0, at + 1);
    return Atom(value);
}
void Event(const std::string& home, const std::string& event,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto path = home + "/server_selection.log";
    struct stat info {};
    if (stat(path.c_str(), &info) == 0 && info.st_size > 512 * 1024) return;
    std::ofstream out(path, std::ios::app);
    if (!out) return;
    out << '[' << Now() << "] ROUTING event=" << Atom(event);
    for (const auto& [key, value] : fields) out << ' ' << Atom(key) << '=' << Atom(value);
    out << '\n';
}
void Begin(const std::string& home, const std::string& mode, const std::string& provider) {
    { std::lock_guard<std::mutex> lock(mutex);
      diagnostics::ArchiveAttempts(home, {"server_selection.log", "session_trace.log"});
      std::ofstream out(home + "/server_selection.log", std::ios::trunc); }
    Event(home, "begin", {{"mode", mode}, {"provider_host", Host(provider)},
        {"probe_kind", "TCP_handshake_not_media_ping"}, {"selection_rule", "mode_dependent_zone_or_regional"}});
}
void Candidates(const std::string& home, const std::vector<StreamRegion>& regions,
    const std::string& source, const std::string& selected) {
    const auto now = Now();
    const auto ranks = server_zone::AutoRanks(regions, now);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        const auto normalized = server_location::NormalizeStreamingBaseUrl(r.url);
        const bool winner = !selected.empty() && normalized == selected;
        const auto score = server_location::RegionSelectionScore(r, now);
        const auto quality = server_location::ObservedQualityPenalty(r, now);
        const auto base = r.selection_score_ms >= 0 ? r.selection_score_ms : r.ping_ms;
        Event(home, "candidate", {{"source", source}, {"order", std::to_string(i)},
            {"name", r.name}, {"host", Host(r.url)}, {"probe_median_ms", std::to_string(r.ping_ms)},
            {"probe_jitter_ms", std::to_string(r.jitter_ms)}, {"tcp_score_ms", std::to_string(r.selection_score_ms)},
            {"probe_degraded", r.probe_degraded ? "1" : "0"},
            {"score_ms", std::to_string(score)},
            {"zone", r.zone_id}, {"auto_rank", std::to_string(ranks[i])},
            {"public_queue_estimate", std::to_string(r.zone_queue_estimate)},
            {"public_queue_fresh", server_zone::FreshQueue(r, now) ? "1" : "0"},
            {"selection_policy", "mixed_endpoints_v2"},
            {"auto_eligible", ranks[i] >= 0 ? "1" : "0"},
            {"zone_catalog_fresh", server_zone::FreshZone(r, now) ? "1" : "0"},
            {"quality_penalty_ms", std::to_string(quality)},
            {"combined_history_adjust_ms", std::to_string(score >= 0 ? score - base : 0)},
            {"allocation_penalty_ms", std::to_string(r.route_history_penalty_ms)},
            {"allocation_samples", std::to_string(r.route_history_samples)},
            {"allocation_redirects", std::to_string(r.route_history_redirects)},
            {"expected_media_rtt_ms", std::to_string(r.route_history_expected_rtt_ms)},
            {"successful_probes", std::to_string(r.successful_probes)}, {"attempted_probes", std::to_string(r.attempted_probes)},
            {"observed_rtt_ms", std::to_string(r.observed_stream_rtt_ms)},
            {"history_actual_host", Host(r.observed_server_host)},
            {"history_weight_percent", std::to_string(server_location::HistoryWeight(r, now))},
            {"observed_p95_ms", std::to_string(r.observed_stream_p95_ms)},
            {"observed_age_ms", std::to_string(r.observed_at_ms > 0 ? now - r.observed_at_ms : -1)},
            {"playback_pauses", std::to_string(r.observed_playback_pause_count)},
            {"playback_pause_ms", std::to_string(r.observed_playback_pause_ms)},
            {"decision", winner ? "selected" : normalized.empty() ? "invalid_endpoint" :
                r.ping_ms < 0 ? "probe_failed" : ranks[i] < 0 ? "outside_eligible_pool" : "alternative"},
            {"route_needs_probe", server_location::ObservedRouteNeedsProbe(r, now) ? "1" : "0"}});
    }
}
} // namespace opennow::routing
