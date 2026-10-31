#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
namespace opennow::video {
struct ReadyFrameMetrics {
    std::uint64_t latency_trims = 0, latency_backoffs = 0, count = 0, age_total_us = 0;
    std::array<std::uint64_t,7> age_buckets {};
    std::array<std::uint64_t,4> depth_buckets {};
    void Observe(std::uint64_t age_us, unsigned depth) {
        ++count; age_total_us += age_us;
        constexpr std::array<std::uint64_t,6> bounds {8000,16000,25000,33000,50000,100000};
        unsigned bucket=0;
        while(bucket<bounds.size() && age_us>bounds[bucket]) ++bucket;
        ++age_buckets[bucket]; ++depth_buckets[std::min(3u,depth)];
    }
};
}
