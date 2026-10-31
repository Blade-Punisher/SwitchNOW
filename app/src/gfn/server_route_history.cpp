#include "server_route_history.hpp"
#include <jansson.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace opennow::server_route {
namespace {
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
std::string Text(json_t* object, const char* key) {
    auto* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : "";
}
std::int64_t Number(json_t* object, const char* key, std::int64_t fallback = 0) {
    auto* value = json_object_get(object, key);
    return json_is_integer(value) ? json_integer_value(value) : fallback;
}
int Bounded(json_t* object, const char* key, int low, int high, int fallback = 0) {
    return static_cast<int>(std::clamp<std::int64_t>(Number(object, key, fallback), low, high));
}
}

std::vector<Observation> ParseHistory(const std::string& text) {
    if (text.empty() || text.size() > 256 * 1024) return {};
    Json root(json_loads(text.c_str(), 0, nullptr), &json_decref);
    if (!root || !json_is_array(root.get())) return {};
    std::vector<Observation> rows;
    for (size_t i = 0; i < std::min<size_t>(32, json_array_size(root.get())); ++i) {
        auto* entry = json_array_get(root.get(), i);
        Observation row;
        row.provider = server_location::NormalizeStreamingBaseUrl(Text(entry, "provider_url"));
        row.requested = server_location::NormalizeStreamingBaseUrl(Text(entry, "requested_region"));
        row.actual = server_location::RouteHost(Text(entry, "actual_host"));
        row.session_id = Text(entry, "cloud_session");
        // Pre-R32 files aggregate allocations but retain just the last RTT.
        // Preserve that evidence as ONE observation, not `sessions` samples.
        if (row.session_id.empty()) row.session_id = "legacy:" + row.actual;
        row.rtt_ms = Bounded(entry, "rtt_ms", -1, 2000, -1);
        row.p95_ms = Bounded(entry, "p95_ms", -1, 2000, -1);
        row.seconds = Bounded(entry, "session_seconds", 0, 86400);
        row.at_ms = Number(entry, "observed_at_ms");
        row.transport_pauses = Bounded(entry, "transport_pauses", 0, 1000000);
        row.transport_pause_ms = Bounded(entry, "transport_pause_ms", 0, 1000000);
        row.media_endpoint = server_location::RouteHost(Text(entry, "media_endpoint"));
        row.playback_pauses = Bounded(entry, "playback_pauses", 0, 1000000);
        row.playback_pause_ms = Bounded(entry, "playback_pause_ms", 0, 86400000);
        row.low_display_ms = Bounded(entry, "low_display_ms", 0, 86400000);
        row.audio_underruns = Bounded(entry, "audio_underruns", 0, 1000000);
        const auto initial = server_location::RouteHost(Text(entry, "initial_host"));
        row.redirected = json_is_true(json_object_get(entry, "redirected")) ||
            (!initial.empty() && initial != row.actual);
        if (!row.provider.empty() && !row.requested.empty() && !row.actual.empty() &&
            row.session_id.size() <= 180) rows.push_back(std::move(row));
    }
    return rows;
}

void Remember(std::vector<Observation>& rows, Observation current) {
    for (const auto& row : rows) {
        if (row.provider == current.provider && row.session_id == current.session_id) {
            current.requested = row.requested;
            current.redirected = current.redirected || row.redirected || row.actual != current.actual;
            break;
        }
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& row) {
        return !Freshness(row.at_ms, current.at_ms) ||
            (row.provider == current.provider && row.session_id == current.session_id);
    }), rows.end());
    rows.insert(rows.begin(), std::move(current));
    if (rows.size() > 32) rows.resize(32);
}

std::string SerializeHistory(const std::vector<Observation>& rows) {
    Json root(json_array(), &json_decref);
    for (size_t i = 0; i < std::min<size_t>(32, rows.size()); ++i) {
        const auto& row = rows[i];
        json_array_append_new(root.get(), json_pack(
            "{s:s,s:s,s:s,s:s,s:i,s:i,s:i,s:I,s:i,s:i,s:b,s:s,s:i,s:i,s:i,s:i}",
            "provider_url", row.provider.c_str(), "requested_region", row.requested.c_str(),
            "actual_host", row.actual.c_str(), "cloud_session", row.session_id.c_str(),
            "rtt_ms", row.rtt_ms, "p95_ms", row.p95_ms, "session_seconds", row.seconds,
            "observed_at_ms", static_cast<json_int_t>(row.at_ms),
            "transport_pauses", row.transport_pauses, "transport_pause_ms", row.transport_pause_ms,
            "redirected", row.redirected, "media_endpoint", row.media_endpoint.c_str(),
            "playback_pauses", row.playback_pauses, "playback_pause_ms", row.playback_pause_ms,
            "low_display_ms", row.low_display_ms, "audio_underruns", row.audio_underruns));
    }
    std::unique_ptr<char, decltype(&std::free)> output(json_dumps(root.get(), JSON_INDENT(2)), &std::free);
    if (!output) throw std::runtime_error("Cannot serialize server route history");
    return output.get();
}
} // namespace opennow::server_route
