#include "server_route_policy.hpp"
#include "models.hpp"
#include <cassert>

int main() {
    using namespace opennow;
    using namespace server_route;
    const std::string provider = "https://provider.example/";
    const std::string poland = "https://eu-poland.cloudmatchbeta.nvidiagrid.net/";
    const std::string paris = "np-par-05.cloudmatchbeta.nvidiagrid.net";
    const std::int64_t now = 1000000000;
    std::vector<Observation> rows;
    Observation row {provider, poland, paris, "one", 47, 55, 500, 0, 0, now, true};
    rows.push_back(row);
    assert(Predict(rows, provider, poland, 26, now).penalty_ms == 2);
    // Repeated transport attempts inside one game are not independent samples.
    rows.push_back(row); rows.push_back(row);
    assert(Predict(rows, provider, poland, 26, now).samples == 1);
    rows[1].session_id = "two";
    assert(Predict(rows, provider, poland, 26, now).penalty_ms == 4);
    rows[2].session_id = "three";
    auto prediction = Predict(rows, provider, poland, 26, now);
    assert(prediction.samples == 3 && prediction.redirects == 3);
    assert(prediction.expected_rtt_ms == 47 && prediction.penalty_ms >= 15);
    StreamRegion a;
    a.name = "Poland"; a.url = poland; a.ping_ms = 26;
    a.selection_score_ms = 27;
    a.route_history_penalty_ms = prediction.penalty_ms;
    a.route_history_at_ms = now;
    assert(server_location::RegionSelectionScore(a, now) > 35);
    assert(Predict(rows, "https://other.example/", poland, 26, now).samples == 0);
    assert(Predict(rows, provider, "https://other.example/", 26, now).samples == 0);
    assert(Predict(rows, provider, poland, -1, now).samples == 0);
    assert(Predict(rows, provider, poland, 26, now - 1).samples == 0);
    assert(Predict(rows, provider, poland, 26, now + 8LL*24*3600*1000).samples == 0);
    const auto old = Predict(rows, provider, poland, 26, now + 2LL*24*3600*1000);
    assert(old.penalty_ms < prediction.penalty_ms);
    rows[1].rtt_ms = 22; rows[1].p95_ms = 24; rows[1].redirected = false;
    rows[1].actual = "np-waw-01.cloudmatchbeta.nvidiagrid.net";
    // Replay two Paris assignments and one good Warsaw allocation. A stable
    // 35 ms alternative can beat Poland's superficially better 26 ms TCP.
    auto mixed = rows;
    auto mixed_prediction = Predict(mixed, provider, poland, 26, now);
    assert(27 + mixed_prediction.penalty_ms > 35);
    rows[2] = rows[1]; rows[2].session_id = "three";
    assert(Predict(rows, provider, poland, 26, now).penalty_ms < prediction.penalty_ms);
    // Only the six most recent independent allocations influence ranking.
    for (int i = 0; i < 6; ++i) {
        auto good = rows[1]; good.at_ms = now + 1; good.session_id = "good" + std::to_string(i);
        rows.push_back(good);
    }
    assert(Predict(rows, provider, poland, 26, now + 1).penalty_ms == 0);
    assert(Predict(rows, provider, poland, 26, now + 1).samples == 6);
    auto uneven = rows;
    for (auto& entry : uneven) {
        entry.playback_pauses = 40; entry.playback_pause_ms = 7000;
        entry.low_display_ms = 60000; entry.audio_underruns = 40;
    }
    assert(Predict(uneven, provider, poland, 26, now + 1).penalty_ms >
        Predict(rows, provider, poland, 26, now + 1).penalty_ms);
    for (auto& entry : rows) { entry.rtt_ms = 300; entry.p95_ms = 2000; }
    assert(Predict(rows, provider, poland, 26, now + 1).penalty_ms == 24);
    for (auto& entry : rows) entry.seconds = 20;
    assert(Predict(rows, provider, poland, 26, now + 1).samples == 0);
    return 0;
}
