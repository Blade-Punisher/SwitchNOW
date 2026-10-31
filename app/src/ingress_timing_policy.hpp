#pragma once
#include <algorithm>
#include <cstdint>

namespace opennow::streaming {
// Application observation times, not NIC/kernel arrival timestamps. A regular
// polling cadence with no RTP progress excludes a stalled application worker,
// but cannot distinguish server pacing, the route, and the OS socket stack.
struct IngressTiming {
    std::uint64_t last_poll_us = 0, last_raw_us = 0, last_au_us = 0;
    std::uint64_t observed_assembly_begin_us = 0;
    std::uint32_t raw_count = 0;
    std::uint64_t Poll(std::uint64_t now) {
        const auto gap = last_poll_us && now >= last_poll_us ? now - last_poll_us : 0;
        last_poll_us = now;
        return gap;
    }
    void Raw(std::uint32_t count, std::uint64_t now) {
        if (count == raw_count) return;
        raw_count = count; last_raw_us = now;
        if (!observed_assembly_begin_us) observed_assembly_begin_us = now;
    }
    std::uint64_t Unit(std::uint64_t now) {
        const auto span = observed_assembly_begin_us && now >= observed_assembly_begin_us
            ? now - observed_assembly_begin_us : 0;
        observed_assembly_begin_us = 0; last_au_us = now;
        return span;
    }
    static std::uint64_t Age(std::uint64_t now, std::uint64_t before) {
        return before && now >= before ? now - before : 0;
    }
};
}
