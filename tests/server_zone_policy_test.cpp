#include "server_zone_policy.hpp"
#include <cassert>

int main() {
    using namespace opennow;
    using namespace server_zone;
    const std::int64_t now = 1792800000000;
    assert(IsZoneId("NP-WAW-01") && IsZoneId("NP-SJC6-04"));
    for (const auto* id : {"NPA-YES-KUL-01", "NP-WAW-01/path", "NP-WAW-01.evil", "NP-WAW-1", "NP-XX-01\n", "np-waw-01", "NP-X-01"})
        assert(!IsZoneId(id));
    assert(ZoneUrl("NP-WAW-01") == "https://np-waw-01.cloudmatchbeta.nvidiagrid.net/");
    assert(ZoneUrl("NP-WAW-01@evil").empty());
    auto region = [&](std::string id, int ping, int queue) {
        StreamRegion r;
        r.zone_id = id; r.name = id; r.url = ZoneUrl(id); r.ping_ms = r.selection_score_ms = ping;
        r.attempted_probes = r.successful_probes = 4;
        r.zone_queue_estimate = queue; r.zone_queue_at_ms = r.zone_catalog_at_ms = now;
        return r;
    };
    auto a = region("NP-WAW-01", 26, 100);
    auto b = region("NP-FRK-06", 30, 0);
    auto far = region("NP-TYO-01", 180, 0);
    StreamRegion regular; regular.name = "Poland";
    regular.url = "https://eu-poland.cloudmatchbeta.nvidiagrid.net/";
    regular.ping_ms = regular.selection_score_ms = 20;
    regular.successful_probes = regular.attempted_probes = 4;
    std::vector<StreamRegion> zones {a, b, far};
    assert(Select(zones, "Closest", now) == a.url);
    assert(Select(zones, "Auto", now) == b.url);
    assert(AutoRank(zones, 2, now) == -1); // far zone cannot win on a short queue
    zones[0].zone_queue_at_ms = now - 600001;
    assert(Select(zones, "Auto", now) == a.url); // no stale/unknown queue advantage
    zones[0].route_history_penalty_ms = 24; zones[0].route_history_at_ms = now;
    assert(Select(zones, "Auto", now) == b.url);
    assert(Select(zones, "Closest", now) == a.url); // Closest remains minimum measured ping
    zones[0].successful_probes = 1;
    assert(Select(zones, "Closest", now) == b.url);
    // A better verified region competes with zones, rather than disappearing.
    zones.push_back(regular);
    assert(Select(zones, "Closest", now) == regular.url);
    assert(Select(zones, "Auto", now) == regular.url);
    for (auto& r : zones) if (!r.zone_id.empty()) r.zone_catalog_at_ms = now - 120001;
    assert(Select(zones, "Auto", now) == regular.url);
    assert(Select(zones, "Closest", now) == regular.url);
    // R40 replay: one slower Sofia zone excluded both fast country endpoints.
    auto sofia = region("NP-SOF-02", 66, -1);
    auto poland = regular; poland.ping_ms = 29; poland.selection_score_ms = 30;
    poland.route_history_penalty_ms = 12; poland.route_history_at_ms = now;
    auto germany = regular; germany.url = "https://eu-germany.cloudmatchbeta.nvidiagrid.net/";
    germany.ping_ms = 40; germany.selection_score_ms = 41;
    germany.route_history_penalty_ms = 4; germany.route_history_at_ms = now;
    std::vector<StreamRegion> replay{sofia, poland, germany};
    assert(AutoRank(replay,1,now)>=0 && AutoRank(replay,2,now)>=0);
    assert(AutoRank(replay,0,now)==-1);
    assert(Select(replay,"Auto",now)==poland.url);
    assert(Select(replay,"Closest",now)==poland.url);
    // Failed and partial probes cannot displace a fully verified route.
    replay[1].successful_probes=0; replay[1].probe_degraded=true;
    assert(Select(replay,"Auto",now)==germany.url);
    replay[2].ping_ms=-1;
    assert(Select(replay,"Auto",now)==sofia.url);
    replay[0].zone_catalog_at_ms=now-120001;
    assert(Select(replay,"Auto",now)==poland.url); // degraded provider fallback still connects
    assert(Select({}, "Auto", now).empty());
    assert(IsOfficialProvider(regular.url));
    assert(!IsOfficialProvider("https://prod.cloudmatchbeta.nvidiagrid.net.evil/"));
    assert(!IsOfficialProvider("https://alliance.example/"));
    for (const auto* host : {"80.84.174.149", "rig01.nvidiagrid.net"}) assert(IsDirectControlHost(host));
    assert(IsDirectControlHost("80-250-98-36.cloudmatchbeta.nvidiagrid.net"));
    assert(IsDirectControlHost("80-250-98-39.cloudmatch.nvidiagrid.net"));
    for (const auto* host : {"127-0-0-1.cloudmatchbeta.nvidiagrid.net", "10-1-2-3.cloudmatchbeta.nvidiagrid.net",
        "80-250-98-999.cloudmatchbeta.nvidiagrid.net", "eu-poland.cloudmatchbeta.nvidiagrid.net",
        "80-250-98-36.cloudmatchbeta.nvidiagrid.net.evil"}) assert(!IsDirectControlHost(host));
    for (const auto* host : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.1", "169.254.1.1", "100.64.0.1", "0.1.2.3", "224.0.0.1",
         "80.-1.2.3", "0177.0.0.1", "80.2.3.256", "80..2.3", "80.2.3.4:443", "80.2.3.4/path", "80.2.3.4\n", "https://80.2.3.4", "other.example",
         "np-waw-01.cloudmatchbeta.nvidiagrid.net", "np-waw-01.cloudmatchbeta.nvidiagrid.net.evil"}) assert(!IsDirectControlHost(host));
    SessionInfo original;
    original.session_id = "game-one"; original.status = 2; original.server_ip = "80.84.174.149";
    assert(ShouldDirectPoll(regular.url, regular.url, original));
    original.status = 1;
    assert(!ShouldDirectPoll(regular.url, regular.url, original));
    original.status = 2;
    assert(!ShouldDirectPoll("https://partner.example/", regular.url, original));
    assert(!ShouldDirectPoll(regular.url, "https://80.84.174.149/", original));
    SessionInfo direct = original;
    direct.signaling_url = "wss://80.84.174.149/nvst/"; direct.media_ip = "80.84.174.149"; direct.media_port = 48322;
    assert(AcceptDirectPoll(original, direct));
    direct.session_id = "another-game";
    assert(!AcceptDirectPoll(original, direct));
    direct.session_id = original.session_id;
    direct.status = 1;
    assert(!AcceptDirectPoll(original, direct));
    direct.status = 2; direct.media_port = 0;
    assert(!AcceptDirectPoll(original, direct));
    direct.media_port = 48322; direct.signaling_url = "ws://unsafe.example/";
    assert(!AcceptDirectPoll(original, direct));
}
