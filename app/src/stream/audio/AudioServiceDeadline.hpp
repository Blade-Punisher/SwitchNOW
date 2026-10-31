#pragma once
#include <algorithm>
#include <cstdint>

namespace opennow::audio {
// One fixed service deadline per extraction, independent of spurious wakes,
// priming and packet reordering. Expiry means service audout, not generate PLC.
struct ServiceDeadline {
    std::uint64_t expires_us;
    explicit ServiceDeadline(std::uint64_t now_us) : expires_us(now_us + 5000) {}
    bool ShouldService(std::uint64_t now_us, bool running, bool reset) const {
        return !running || reset || now_us >= expires_us;
    }
    std::uint64_t WaitUs(std::uint64_t now_us, std::uint64_t desired_us) const {
        return now_us >= expires_us ? 0 : std::min(desired_us, expires_us - now_us);
    }
};
}
