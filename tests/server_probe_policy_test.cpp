#include "server_probe_policy.hpp"
#include "server_zone_policy.hpp"
#include <cassert>
int main() {
    using namespace opennow;
    using namespace server_probe;
    assert(TcpEndpoint("https://eu-poland.cloudmatchbeta.nvidiagrid.net/") ==
        "http://eu-poland.cloudmatchbeta.nvidiagrid.net:443/");
    assert(TcpEndpoint("https://example.org:8443/") == "http://example.org:8443/");
    assert(TcpEndpoint("http://example.org/").empty()); // accept only validated HTTPS service inputs
    assert(TcpEndpoint("invalid").empty());
    assert(TcpEndpoint("https://user:pass@example.org/").empty());
    assert(TcpEndpoint("https://example.org?secret=1").empty());
    assert(TcpEndpoint("https://example.org#fragment").empty());
    assert(!CommonFailure(3,3)); assert(CommonFailure(4,3)); assert(!CommonFailure(5,3));
    StreamRegion coarse;
    coarse.name = coarse.zone_id = "NP-WAW-01";
    coarse.url = server_zone::ZoneUrl(coarse.zone_id);
    coarse.zone_catalog_at_ms = 1000000;
    coarse.ping_ms = 25; coarse.selection_score_ms = 125;
    coarse.successful_probes = coarse.attempted_probes = 1;
    auto preserved = Finish(coarse, {}, 3, true);
    assert(preserved.ping_ms == 25 && preserved.probe_degraded && preserved.attempted_probes == 4);
    assert(preserved.selection_score_ms > coarse.selection_score_ms);
    assert(server_zone::Select({preserved}, "Auto", 1000001) == coarse.url);
    assert(server_zone::Select({coarse}, "Auto", 1000001).empty());
    assert(Finish(coarse, {}, 3, false).ping_ms < 0); // isolated failure stays excluded
    auto refined = Finish(coarse, {26,24,25}, 3, false);
    assert(refined.ping_ms == 25 && !refined.probe_degraded && refined.successful_probes == 3);
        refined.zone_id = refined.name = "NP-FRK-06";
    refined.url = server_zone::ZoneUrl(refined.zone_id); refined.ping_ms = 30;
    assert(server_zone::Select({preserved, refined}, "Closest", 1000001) == refined.url);
    auto incomplete = Finish(coarse, {}, 0, false);
    assert(incomplete.ping_ms == 25 && incomplete.probe_degraded);
    auto partial = Finish(coarse, {27}, 3, false);
    assert(partial.ping_ms == 26 && partial.probe_degraded && partial.successful_probes == 2);
    StreamRegion regular = refined;
    regular.zone_id.clear(); regular.url = "https://eu-germany.cloudmatchbeta.nvidiagrid.net/";
    assert(server_zone::Select({partial, regular}, "Auto", 1000001) == regular.url);
    assert(server_location::ObservedRouteNeedsProbe(partial,1000001));
    coarse.ping_ms = -1; coarse.successful_probes = 0;
    assert(Finish(coarse, {}, 3, true).ping_ms < 0); // never invent a successful sample

    std::vector<StreamRegion> large(80);
    for (size_t i = 0; i < 40; ++i) large[i].name = std::to_string(i);
    for (size_t i = 40; i < large.size(); ++i) large[i].zone_id = "NP-FRK-06";
    auto candidates = CoarseCandidates(large, 1000000);
    assert(candidates.size() == 24 && candidates.front() == 0 && candidates.back() == 39);
    for (auto index : candidates) assert(index < 40); // cold optional zones never flood sockets
    large[50].observed_stream_rtt_ms = 20; large[50].observed_at_ms = 999999;
    large[51].observed_stream_rtt_ms = 25; large[51].observed_at_ms = 999999;
    large[52].observed_stream_rtt_ms = 10; large[52].observed_at_ms = 999999;
    candidates = CoarseCandidates(large, 1000000);
    assert(candidates.size() == 26 && candidates[24] == 52 && candidates[25] == 50);
    assert(CoarseCandidates(large, 100000000).size() == 24); // expired observations excluded
}
