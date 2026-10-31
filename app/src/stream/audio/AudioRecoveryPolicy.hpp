#pragma once
#include <algorithm>
#include <cstdint>

namespace opennow::audio {
struct ArrivalJitter {
    bool initialized = false;
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    std::uint64_t arrival_us = 0, jitter_us = 0;
    void Observe(std::uint16_t seq, std::uint32_t ts, std::uint64_t now) {
        if (initialized) {
            const auto distance = static_cast<std::int16_t>(seq - sequence);
            if (distance <= 0 || now < arrival_us) return;
            const auto ticks = static_cast<std::int32_t>(ts - timestamp);
            if (distance == 1 && ticks > 0 && ticks <= 4800 && now - arrival_us < 500000) {
                const auto expected = static_cast<std::uint64_t>(ticks) * 1000000 / 48000;
                const auto actual = now - arrival_us;
                const auto deviation = actual > expected ? actual - expected : expected - actual;
                jitter_us = (jitter_us * 15 + std::min<std::uint64_t>(deviation, 100000)) / 16;
            }
        }
        initialized = true; sequence = seq; timestamp = ts; arrival_us = now;
    }
};
inline int RecoveryReserve(int current, int base, int maximum, unsigned episode_count,
                           std::uint64_t jitter_us, std::uint64_t gap_ms) {
    if (gap_ms >= 150) return current;
    const int extra = jitter_us >= 15000 && episode_count >= 3 ? 3 :
        jitter_us >= 10000 ? 2 : jitter_us >= 5000 ||
        (episode_count >= 2 && gap_ms >= static_cast<unsigned>(base * 10)) ? 1 : 0;
    // Repeated outages alone cannot ratchet the target to its ceiling.
    return std::clamp(std::max(current, base + extra), base, maximum);
}
inline bool CanRestartOutput(unsigned queued, int desired, int base,
                             std::uint64_t now, std::uint64_t restart) {
    if (queued >= static_cast<unsigned>(desired)) return true;
    // A low packet rate must not turn the larger reserve into an unbounded
    // extra wait. Keep at least the configured base, only after an underrun.
    return restart && now >= restart && now - restart >= 150000 &&
        queued >= static_cast<unsigned>(base);
}

// Repeated short interruptions may exceed an EWMA jitter estimate. A bounded
// temporary target protects subsequent restarts; it never changes live PCM.
struct ShortGapReserve {
    std::uint64_t last_us = 0, active_until_us = 0;
    std::uint64_t longest_ms = 0;
    unsigned recent = 0;
    void Observe(std::uint64_t now, std::uint64_t gap_ms, int base) {
        (void)base;
        if (gap_ms >= 150 || gap_ms < 20) return;
        if (!last_us || now < last_us || now - last_us > 5000000) { recent = 0; longest_ms = 0; }
        last_us = now;
        longest_ms = std::max(longest_ms, gap_ms);
        recent = std::min(12u, recent + 1);
        if (recent >= 3) active_until_us = now + 10000000;
    }
    int Demand(std::uint64_t now, int base, int maximum, int jitter_demand) const {
        const bool active = active_until_us && now >= last_us && now < active_until_us;
        const int tail_target = active && recent >= 6
            ? std::min(base + 5, static_cast<int>((longest_ms + 29) / 10)) : base;
        return std::clamp(std::max({jitter_demand, base + (active ? 2 : 0), tail_target}), base, maximum);
    }
};
// An already running audout can accept PCM after a brief empty queue. Do not
// turn every short starvation into a stop/start and a full priming delay.
// A running empty device does not buffer new PCM. A slightly longer grace
// can resume immediately on a late block instead of paying another full prime.
inline std::uint64_t EmptyGraceBudgetUs(std::uint64_t jitter_us,
    std::uint64_t media_gap_ms, unsigned recent_underruns) {
    if (recent_underruns < 2 || media_gap_ms >= 150) return 20000;
    return 20000 + std::min<std::uint64_t>(20000, std::min<std::uint64_t>(jitter_us, 10000) * 2);
}
struct EmptyQueueGrace {
    std::uint64_t began_us = 0, resumed = 0, silence_us = 0, maximum_us = 0;
    bool MustReprime(std::uint64_t now, std::uint64_t budget_us = 20000) {
        if (!began_us || now < began_us) { began_us = now; return false; }
        return now - began_us >= std::clamp<std::uint64_t>(budget_us, 20000, 40000);
    }
    void Filled(std::uint64_t now) {
        if (began_us && now >= began_us) {
            const auto duration = now - began_us;
            ++resumed; silence_us += duration; maximum_us = std::max(maximum_us, duration);
        }
        began_us = 0;
    }
    void Reprimed() { began_us = 0; }
};
// Lower only the future restart target after sustained lower demand.
// PCM already submitted to audout is never discarded.
struct ReserveRelease {
    std::uint64_t lower_since_us = 0;
    int Observe(std::uint64_t now, int current, int demand) {
        if (demand >= current) { lower_since_us = 0; return current; }
        if (!lower_since_us || now < lower_since_us) { lower_since_us = now; return current; }
        if (now - lower_since_us < 30000000) return current;
        lower_since_us = now;
        return std::max(demand, current - 1);
    }
};
}
