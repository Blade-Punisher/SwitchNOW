#include "network_resource_policy.hpp"
#include <cassert>
#include <functional>
#include <vector>

int main() {
    using namespace opennow::network;
    using namespace std::chrono;
    const auto start = Clock::time_point{} + hours(10);
    ResourceState state;
    assert(state.TakeProbe(start));
    assert(!state.TakeProbe(start + milliseconds(349)));
    assert(state.TakeProbe(start + milliseconds(350)));
    state.Exhausted(start + seconds(1));
    assert(!state.Ready(start + milliseconds(5999)));
    assert(state.Ready(start + seconds(6)));
    assert(!state.TakeProbe(start + seconds(3)));
    state.Exhausted(start + seconds(2));
    assert(!state.Ready(start + milliseconds(6999)));
    assert(state.Ready(start + seconds(7)));

    ResourceState rolling;
    for (int i = 0; i < 32; ++i) assert(rolling.TakeProbe(start + milliseconds(i * 350)));
    assert(!rolling.TakeProbe(start + seconds(40)));
    assert(rolling.TakeProbe(start + seconds(60)));
    assert(!rolling.TakeProbe(start + milliseconds(60349)));
    assert(rolling.TakeProbe(start + milliseconds(60350)));
    // A quiet gap does not reset the limit at a fixed window boundary.
    ResourceState boundary;
    assert(boundary.TakeProbe(start));
    for (int i = 0; i < 31; ++i) assert(boundary.TakeProbe(start + seconds(29 + i)));
    assert(boundary.TakeProbe(start + seconds(60)));
    assert(!boundary.TakeProbe(start + seconds(61)));
    assert(ResourceError(ENOBUFS) && ResourceError(ENOMEM) && ResourceError(EMFILE));
    assert(!ResourceError(ECONNREFUSED));
    assert(SafeResourceRetry(7, ENOBUFS, 0, 0));
    assert(SafeResourceRetry(7, ENOBUFS, 0, 1));
    assert(!SafeResourceRetry(7, ENOBUFS, 0, 2));
    assert(!SafeResourceRetry(7, ENOBUFS, 1, 0)); // uncertain POST must never replay
    assert(!SafeResourceRetry(28, ENOBUFS, 0, 0)); // timeout is ambiguous
    assert(!SafeResourceRetry(7, ECONNREFUSED, 0, 0));

    assert(ProbeAllowed());
    {
        LaunchPriority launch;
        assert(ProbeAllowed()); // synchronous selection belonging to this launch
        std::atomic<bool> background_allowed {true};
        std::thread background([&] { background_allowed = ProbeAllowed(); });
        background.join(); assert(!background_allowed);
        { LaunchPriority nested; assert(ProbeAllowed() && launch_count == 2); }
        assert(launch_count == 1);
    }
    assert(launch_count == 0 && ProbeAllowed());
    {
        std::unique_lock<std::mutex> first(sweep_mutex, std::try_to_lock);
        assert(first);
        std::atomic<bool> duplicate {true};
        std::thread other([&] {
            std::unique_lock<std::mutex> second(sweep_mutex, std::try_to_lock);
            duplicate = second.owns_lock();
        });
        other.join(); assert(!duplicate);
    }
    ResourceFailure(ENOBUFS);
    assert(!ProbeAllowed());
    assert(!TakeProbeSlot(Clock::now() + seconds(1)));
    const std::function<bool()> cancelled = [] { return true; };
    assert(!WaitForResources(cancelled));
    { std::lock_guard<std::mutex> lock(resource_mutex); resource_state = {}; }
    const std::function<bool()> no_cancel;
    assert(WaitForResources(no_cancel));
}
