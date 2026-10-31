#pragma once
#include <atomic>
#include <cstdint>

namespace opennow::performance {
// The producer never waits for a diagnostics reader. An observation racing
// Take() belongs to either the ending interval or the following one.
class IntervalMaximum {
public:
    void Observe(std::uint64_t value) {
        auto previous = maximum_.load(std::memory_order_relaxed);
        while (previous < value && !maximum_.compare_exchange_weak(
            previous, value, std::memory_order_relaxed)) {}
    }
    std::uint64_t Take() const { return maximum_.exchange(0, std::memory_order_relaxed); }
private:
    mutable std::atomic<std::uint64_t> maximum_ {0};
};
}
