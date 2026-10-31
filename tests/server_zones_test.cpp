#include "gfn/server_zones.hpp"
#include "server_zone_policy.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc, char** argv) {
    using namespace opennow;
    const std::int64_t now = 1792800000000;
    const std::string queue = R"({"status":true,"data":{
        "NP-WAW-01":{"QueuePosition":85,"Last Updated":1792800000},
        "NP-PAR-05":{"QueuePosition":7,"Last Updated":1792700000},
        "NP-AMS-08":{"QueuePosition":-1,"Last Updated":1792800000},
        "NP-FRK-06":{"QueuePosition":8,"Last Updated":1792800000000},
        "NP-LON-05":{"QueuePosition":3,"Last Updated":1792900000},
        "NP-SOF-02":{"QueuePosition":1,"Last Updated":1792800000},
        "NPA-YES-KUL-01":{"QueuePosition":0,"Last Updated":1792800000},
        "NP-WAW-01@evil":{"QueuePosition":0,"Last Updated":1792800000}}})";
    const std::string mapping = R"({"status":true,"data":{
        "NP-WAW-01":{"nuked":false},"NP-PAR-05":{"nuked":false},
        "NP-AMS-08":{"nuked":true},"NP-FRK-06":{},"NP-LON-05":{"nuked":"false"},
        "NPA-YES-KUL-01":{"nuked":false},"NP-WAW-01@evil":{"nuked":false}}})";
    gfn::ZoneCatalogDiagnostics diagnostics;
    const auto rows = gfn::ParseServerZones(queue, mapping, now, &diagnostics);
    assert(diagnostics.fresh == 2 && diagnostics.stale == 1 && diagnostics.newest_age_ms == 0);
    assert(rows.size() == 3);
    assert(rows[0].zone_id == "NP-FRK-06" && rows[0].zone_queue_estimate == 8);
    assert(rows[1].zone_id == "NP-PAR-05" && rows[1].zone_queue_estimate == -1);
    assert(rows[2].zone_id == "NP-WAW-01" && rows[2].zone_queue_estimate == 85);
    assert(rows[2].url == server_zone::ZoneUrl("NP-WAW-01"));
    assert(server_zone::FreshZone(rows[2], now) && server_zone::FreshQueue(rows[2], now));
    assert(!server_zone::FreshQueue(rows[2], now + 600001));
    assert(gfn::ParseServerZones("invalid", mapping, now).empty());
    assert(gfn::ParseServerZones(queue, "{}", now).empty());
    assert(gfn::ParseServerZones(queue, R"({"status":false,"data":{}})", now).empty());
    assert(gfn::ParseServerZones(std::string(600000, 'x'), mapping, now).empty());
    if (argc == 3) {
        auto read = [](const char* file) { std::ifstream in(file); return std::string(std::istreambuf_iterator<char>(in), {}); };
        const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto live = gfn::ParseServerZones(read(argv[1]), read(argv[2]), time);
        assert(!live.empty());
        std::cout << "Live public catalog parsed: " << live.size() << " eligible zones; fresh queues: "
            << std::count_if(live.begin(), live.end(), [&](const auto& r) { return server_zone::FreshQueue(r, time); }) << '\n';
    }
}
