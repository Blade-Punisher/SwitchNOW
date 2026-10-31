#include "gfn/server_route_history.hpp"
#include <cassert>

int main() {
    using namespace opennow::server_route;
    const std::int64_t now = 1000000000;
    std::vector<Observation> rows;
    Observation row {"https://provider.example/", "https://region.example/", "actual.example",
        "one", 47, 55, 500, 4, 450, now, true};
    row.media_endpoint = "media.example"; row.playback_pauses = 12;
    row.playback_pause_ms = 2400; row.low_display_ms = 30000; row.audio_underruns = 10;
    Remember(rows, row);
    auto parsed = ParseHistory(SerializeHistory(rows));
    assert(parsed.size() == 1 && parsed[0].session_id == "one");
    assert(parsed[0].transport_pauses == 4 && parsed[0].transport_pause_ms == 450);
    assert(parsed[0].at_ms == now && parsed[0].redirected);
    assert(parsed[0].media_endpoint == row.media_endpoint && parsed[0].low_display_ms == 30000);
    assert(parsed[0].playback_pause_ms == 2400 && parsed[0].audio_underruns == 10);
    row.rtt_ms = 40; row.at_ms++;
    Remember(rows, row);
    assert(rows.size() == 1 && rows[0].rtt_ms == 40);
    const auto requested = row.requested;
    row.requested = "https://actual.example/";
    Remember(rows, row);
    assert(rows.size() == 1 && rows[0].requested == requested);
    row.requested = requested;
    for (int i = 0; i < 40; ++i) {
        row.session_id = std::to_string(i); ++row.at_ms;
        Remember(rows, row);
    }
    assert(rows.size() == 32 && ParseHistory(SerializeHistory(rows)).size() == 32);
    row.at_ms += 8LL*24*3600*1000;
    Remember(rows, row);
    assert(rows.size() == 1);
    assert(ParseHistory("invalid").empty());
    assert(ParseHistory("{}").empty());
    assert(ParseHistory(std::string(300000, 'x')).empty());
    const auto legacy = ParseHistory(R"([{"provider_url":"https://provider.example/",
      "requested_region":"https://region.example/","actual_host":"actual.example",
      "sessions":999,"rtt_ms":47,"p95_ms":55,"session_seconds":500,"observed_at_ms":1000000000}])");
    assert(legacy.size() == 1);
    assert(Predict(legacy, row.provider, row.requested, 26, now).samples == 1);
    assert(Predict(legacy, row.provider, row.requested, 26, now).penalty_ms == 0);
    // An unknown provider alias alone is not proof of a reassignment.
    assert(!legacy[0].redirected);
    return 0;
}
