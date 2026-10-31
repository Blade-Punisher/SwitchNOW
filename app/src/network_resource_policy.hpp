#pragma once
#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <thread>

namespace opennow::network {
using Clock = std::chrono::steady_clock;
inline bool ResourceError(int error) {
    return error == ENOBUFS || error == ENOMEM || error == EMFILE || error == ENFILE;
}
// A POST may be retried only if every actual socket creation failed. A
// timeout after opening even one socket remains ambiguous and is never replayed.
inline bool SafeResourceRetry(int code, int error, unsigned opened, unsigned attempt) {
    return code == 7 && ResourceError(error) && opened == 0 && attempt < 2;
}
struct ResourceState {
    Clock::time_point cooldown {}, next_probe {};
    std::array<Clock::time_point, 32> probe_times {};
    unsigned window_probes = 0, head = 0;
    bool Ready(Clock::time_point now) const { return now >= cooldown; }
    void Exhausted(Clock::time_point now) {
        cooldown = std::max(cooldown, now + std::chrono::seconds(5));
    }
    bool TakeProbe(Clock::time_point now) {
        if (!Ready(now) || now < next_probe) return false;
        while (window_probes && now - probe_times[head] >= std::chrono::seconds(60)) {
            head = (head + 1) % probe_times.size(); --window_probes;
        }
        if (window_probes >= 32) return false;
        probe_times[(head + window_probes) % probe_times.size()] = now;
        ++window_probes;
        next_probe = now + std::chrono::milliseconds(350);
        return true;
    }
};
inline std::mutex resource_mutex, sweep_mutex;
inline ResourceState resource_state;
inline std::atomic<unsigned> launch_count {0};
inline thread_local unsigned own_launch = 0;
class LaunchPriority {
public:
    LaunchPriority() { ++own_launch; ++launch_count; }
    ~LaunchPriority() { --launch_count; --own_launch; }
    LaunchPriority(const LaunchPriority&) = delete;
    LaunchPriority& operator=(const LaunchPriority&) = delete;
};
inline bool ProbeAllowed() {
    std::lock_guard<std::mutex> lock(resource_mutex);
    return resource_state.Ready(Clock::now()) && (launch_count == 0 || own_launch != 0);
}
inline void ResourceFailure(int error) {
    if (!ResourceError(error)) return;
    std::lock_guard<std::mutex> lock(resource_mutex);
    resource_state.Exhausted(Clock::now());
}
inline bool TakeProbeSlot(Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(resource_mutex);
            const auto now = Clock::now();
            if (!resource_state.Ready(now) || (launch_count != 0 && own_launch == 0)) return false;
            if (resource_state.TakeProbe(now)) return true;
            if (resource_state.window_probes >= 32) return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
template<class Cancel> bool WaitForResources(const Cancel& cancelled) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(5500);
    for (;;) {
        if (cancelled && cancelled()) return false;
        { std::lock_guard<std::mutex> lock(resource_mutex);
          if (resource_state.Ready(Clock::now())) return true; }
        if (Clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
}
