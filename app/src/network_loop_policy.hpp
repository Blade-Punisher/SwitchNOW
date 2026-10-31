#pragma once
#include <cstdint>

namespace opennow::network
{

constexpr int LoopBackoffMilliseconds(bool ready, int processed_datagrams)
{
    if (processed_datagrams > 0)
        return 0;
    return ready ? 1 : 2;
}

// A report left over from an earlier pass may not change the wake generation.
// Do not sleep with sendable work already queued. A full SCTP window retains
// the normal idle wait until its existing retry deadline.
constexpr bool QueuedInputCanRun(bool transport_ready, bool input_ready,
                                 bool queued, std::uint64_t now,
                                 std::uint64_t backpressure_until) {
    return transport_ready && input_ready && queued && now >= backpressure_until;
}

} // namespace opennow::network
