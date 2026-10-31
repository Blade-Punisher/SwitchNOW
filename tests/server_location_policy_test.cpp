#include "server_location_policy.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace
{
struct RegionCandidate
{
    std::string name;
    std::string url;
    int ping_ms;
    int selection_score_ms = -1;
    int observed_stream_rtt_ms = -1;
    int observed_stream_p95_ms = -1;
    int observed_transport_pause_count = 0;
    int observed_transport_pause_ms = 0;
    int observed_session_seconds = 0;
    std::int64_t observed_at_ms = 0;
    int observed_playback_pause_count = 0;
    int observed_playback_pause_ms = 0;
    std::string observed_server_host = {};
};
}

int main()
{
    using namespace opennow::server_location;
    assert(IsProbeFresh(1000, 301000, 300000));
    assert(!IsProbeFresh(1000, 301001, 300000));
    assert(!IsProbeFresh(1001, 1000, 300000));
    assert(!IsProbeFresh(0, 1000, 300000));
    RegionCandidate logged {"Bulgaria", "https://bg.example/", 25};
    logged.observed_server_host = logged.url;
    logged.observed_session_seconds = 500;
    logged.observed_at_ms = 10000;
    logged.observed_playback_pause_count = 148;
    logged.observed_playback_pause_ms = 18922;
    assert(ObservedRouteNeedsProbe(logged, 11000));
    assert(CachedRouteNeedsProbe(std::vector{logged}, logged.url, 11000));
    logged.observed_playback_pause_count = 3;
    logged.observed_playback_pause_ms = 500;
    assert(!ObservedRouteNeedsProbe(logged, 11000));
    logged.observed_playback_pause_count = 148;
    assert(!ObservedRouteNeedsProbe(logged, 10000 + 8LL * 24 * 60 * 60 * 1000));
    assert(CachedRouteNeedsProbe(std::vector{logged}, "https://missing.example/", 11000));
    RegionCandidate playback {"Test", "https://test.example", 25};
    playback.observed_server_host = playback.url;
    playback.observed_session_seconds = 3600;
    playback.observed_at_ms = 10000;
    playback.observed_playback_pause_count = 75;
    playback.observed_playback_pause_ms = 15000;
    assert(ObservedQualityPenalty(playback, 11000) == 0);
    playback.observed_transport_pause_count = 75;
    playback.observed_transport_pause_ms = 15000;
    assert(ObservedQualityPenalty(playback, 11000) > 0);
    assert(ObservedQualityPenalty(playback, 10000 + 8LL * 24 * 3600 * 1000) == 0);

    assert(IsAutomatic(""));
    assert(IsAutomatic("Auto"));
    assert(IsAutomatic("Closest") && IsClosest("Closest"));
    assert(!IsAutomatic("https://np-ams.example/"));

    assert(IsValidStreamingBaseUrl("https://np-ams.example/"));
    assert(IsValidStreamingBaseUrl("https://np-ams.example:443/path"));
    assert(!IsValidStreamingBaseUrl("http://np-ams.example/"));
    assert(!IsValidStreamingBaseUrl("https://user@np-ams.example/"));
    assert(!IsValidStreamingBaseUrl("https://np-ams.example/\nHeader: value"));

    assert(
        NormalizeStreamingBaseUrl("https://np-ams.example") ==
        "https://np-ams.example/");
    assert(NormalizeStreamingBaseUrl("not a URL").empty());

    assert(
        ResolveStreamingBaseUrl("Auto", "https://prod.example") ==
        "https://prod.example/");
    assert(
        ResolveStreamingBaseUrl(
            "Auto",
            "https://prod.example",
            "https://np-ams.example") ==
        "https://np-ams.example/");
    assert(
        ResolveStreamingBaseUrl(
            "https://np-lon.example", "https://prod.example/") ==
        "https://np-lon.example/");
    assert(
        ResolveStreamingBaseUrl(
            "http://unsafe.example", "https://prod.example/") ==
        "https://prod.example/");

    const std::vector<RegionCandidate> regions = {
        {"London", "https://np-lon.example", 28},
        {"Amsterdam", "https://np-ams.example", 10},
        {"Paris", "https://np-par.example", -1},
        {"Unsafe", "http://unsafe.example", 2},
    };
    assert(
        SelectBestMeasuredRegionUrl(regions) ==
        "https://np-ams.example/");
    const std::vector<RegionCandidate> unavailable_regions = {
        {"Paris", "https://np-par.example", -1},
    };
    assert(SelectBestMeasuredRegionUrl(unavailable_regions).empty());

    const auto stable = EstimateLatency({38, 39, 40, 41, 42}, 5);
    assert(stable.median_ms == 40);
    assert(stable.jitter_ms == 2);
    assert(stable.selection_score_ms == 41);
    assert(stable.successful_probes == 5);

    const auto incomplete = EstimateLatency({28, -1, 32, -1, 30}, 5);
    assert(incomplete.median_ms == 30);
    assert(incomplete.successful_probes == 3);
    assert(incomplete.selection_score_ms > stable.selection_score_ms);

    const std::vector<RegionCandidate> scored_regions = {
        {"Unstable", "https://fast-but-unstable.example", 28, 80},
        {"Stable", "https://stable.example", 38, 40},
    };
    assert(
        SelectBestMeasuredRegionUrl(scored_regions) ==
        "https://stable.example/");

    // A region that looked fast over TCP but measured slow during an actual
    // stream should lose to a similarly responsive untried region.
    std::vector<RegionCandidate> observed_regions = {
        {"Old", "https://old.example", 24, 25, 58},
        {"New", "https://new.example", 30, 31, -1},
    };
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    observed_regions[0].observed_at_ms = now;
    observed_regions[0].observed_session_seconds = 600;
    observed_regions[0].observed_server_host = observed_regions[0].url;
    assert(RegionSelectionScore(observed_regions[0]) == 36);
    assert(RegionSelectionScore(observed_regions[0], now + 8LL*24*3600*1000) == 25);
    RegionCandidate poor = observed_regions[0];
    poor.observed_stream_p95_ms = 105;
    poor.observed_transport_pause_count = 8;
    poor.observed_transport_pause_ms = 4000;
    poor.observed_session_seconds = 600;
    poor.observed_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    assert(RegionSelectionScore(poor) > RegionSelectionScore(observed_regions[0]));
    assert(ObservedQualityPenalty(poor, poor.observed_at_ms +
        8LL * 24 * 60 * 60 * 1000) == 0);
    assert(SelectBestMeasuredRegionUrl(observed_regions) ==
           "https://new.example/");

    const std::vector<RegionCandidate> coarse_regions = {
        {"Bulgaria", "https://np-sof.example", 15},
        {"Frankfurt", "https://np-fra.example", 31},
        {"London", "https://np-lon.example", 72},
        {"Invalid", "http://invalid.example", 1},
        {"Offline", "https://offline.example", -1},
    };
    const std::vector<size_t> finalists =
        SelectLatencyFinalists(coarse_regions, 5, 35);
    assert(finalists.size() == 2);
    assert(finalists[0] == 0);
    assert(finalists[1] == 1);

    const std::vector<size_t> one_finalist =
        SelectLatencyFinalists(coarse_regions, 1, 100);
    assert(one_finalist.size() == 1);
    assert(one_finalist[0] == 0);
    // A failed nearby location gets another chance even with healthy peers.
    const auto retries = SelectLatencyRetries(coarse_regions);
    assert(retries.size() == 1 && retries[0] == 4);
    assert(SelectLatencyRetries(unavailable_regions).size() == 1);
    auto recovered = coarse_regions;
    recovered[4].ping_ms = 10;
    assert(SelectLatencyRetries(recovered).empty());
    assert(SelectLatencyFinalists(recovered, 5, 35).front() == 4);
    const std::string suffix = ".cloudmatchbeta.nvidiagrid.net";
    const auto warsaw = "np-waw-01" + suffix;
    const auto paris = "np-par-04" + suffix;
    std::vector<RegionCandidate> replay = {
        {"Poland", "https://eu-poland" + suffix + "/", 24, 25},
        {"France2", "https://eu-france-2" + suffix + "/", 44, 44},
        {"France1", "https://eu-france-1" + suffix + "/", 44, 44}};
    assert(HistoryMatchesRegion(replay[0].url, warsaw));
    assert(!HistoryMatchesRegion(replay[1].url, warsaw));
    assert(!HistoryMatchesRegion("https://eu-poland" + suffix + ".invalid/", warsaw));
    assert(!HistoryMatchesRegion("https://partner.example/", warsaw));
    assert(!HistoryMatchesRegion("https://[2001:db8::1]/", "[2001:db8::2]:443"));
    assert(HistoryMatchesRegion("https://[2001:db8::1]/", "[2001:db8::1]:443"));
    assert(ObservedRegionIndex(replay, replay[1].url, warsaw) == 0);
    assert(ObservedRegionIndex(replay, replay[1].url, paris) == 1);
    assert(ObservedRegionIndex(replay, replay[0].url, paris) == replay.size());
    // Exact PAR node wins over two ambiguous country aliases when saving metrics.
    auto exact = replay[1]; exact.url = "https://" + paris + "/";
    replay.push_back(exact);
    assert(ObservedRegionIndex(replay,replay[0].url,paris)==3);
    replay.pop_back();
    auto rare = poor;
    rare.observed_transport_pause_count=4; rare.observed_transport_pause_ms=966;
    rare.observed_session_seconds=966; rare.observed_stream_p95_ms=rare.ping_ms;
    rare.observed_at_ms=now;
    assert(ObservedQualityPenalty(rare,now)>=1);
    auto& poland = replay[0];
    poland.observed_at_ms = now - 157676779;
    poland.observed_session_seconds = 1000;
    poland.observed_stream_rtt_ms = 39;
    poland.observed_stream_p95_ms = 76;
    poland.observed_playback_pause_count = 218;
    poland.observed_playback_pause_ms = 32126;
    poland.observed_server_host = warsaw;
    auto& france = replay[1];
    france.observed_at_ms = now;
    france.observed_session_seconds = 679;
    france.observed_stream_rtt_ms = 28;
    france.observed_server_host = warsaw;
    assert(HistoryWeight(poland, now) == 25);
    assert(HistoryWeight(france, now) == 0);
    assert(RegionSelectionScore(france, now) == 44);
    assert(RegionSelectionScore(poland, now) < 35);
    assert(SelectBestMeasuredRegionUrl(replay) == poland.url);
    assert(HistoryWeight(poland, now - 200000000) == 0);
    return 0;
}
