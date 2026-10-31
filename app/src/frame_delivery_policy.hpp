#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace opennow::streaming {
struct FrameDelivery {
    static constexpr std::array<std::uint64_t, 11> limits {
        5000,10000,16667,20000,25000,33334,50000,100000,250000,500000,1000000};
    std::array<std::uint64_t, 12> histogram {};
    std::uint64_t samples = 0, jitter_us = 0, over33 = 0, over50 = 0, over100 = 0;
    void Observe(std::uint64_t interval, std::int32_t ticks, int expected_ticks) {
        if (ticks <= 0 || expected_ticks <= 0) return; // duplicate/reordered AU
        ++samples;
        ++histogram[std::lower_bound(limits.begin(), limits.end(), interval) - limits.begin()];
        over33 += interval > 33334; over50 += interval > 50000; over100 += interval > 100000;
        // Source timestamp skips and long outages are not ordinary delivery jitter.
        // Keep them in tail statistics but never train the small wait reserve on them.
        if (ticks <= expected_ticks * 3 / 2 && ticks >= expected_ticks / 2 && interval < 100000) {
            const auto expected = static_cast<std::uint64_t>(ticks) * 1000000 / 90000;
            const auto deviation = interval > expected ? interval - expected : expected - interval;
            jitter_us = (jitter_us * 15 + std::min<std::uint64_t>(deviation, 30000)) / 16;
        }
    }
    std::uint64_t P95() const {
        if (!samples) return 0;
        std::uint64_t count = 0;
        for (unsigned i = 0; i < histogram.size(); ++i) {
            count += histogram[i];
            if (count >= (samples * 95 + 99) / 100)
                return i < limits.size() ? limits[i] : 2000000;
        }
        return 0;
    }
};
}
