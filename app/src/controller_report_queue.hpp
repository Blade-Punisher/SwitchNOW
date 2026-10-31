#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace opennow::input
{
// Local queue key only. Triggers occupy separate fields in the wire protocol;
// their press/release transitions must not be coalesced as axis-only changes.
constexpr std::uint32_t ControllerEdgeState(std::uint16_t buttons,
    std::uint8_t left_trigger, std::uint8_t right_trigger) {
    return buttons | (std::uint32_t(left_trigger != 0) << 16) |
        (std::uint32_t(right_trigger != 0) << 17);
}

enum class ReportQueueResult
{
    Appended,
    CoalescedAxes,
    DroppedOldestEdge,
};

template <typename Report>
ReportQueueResult QueueControllerReport(
    std::deque<Report>& queue, Report report, std::size_t maximum_edges)
{
    if (!queue.empty() && queue.back().buttons == report.buttons)
    {
        queue.back() = std::move(report);
        return ReportQueueResult::CoalescedAxes;
    }
    if (maximum_edges == 0)
        return ReportQueueResult::DroppedOldestEdge;
    const bool overflow = queue.size() >= maximum_edges;
    if (overflow)
        queue.pop_front();
    queue.push_back(std::move(report));
    return overflow ? ReportQueueResult::DroppedOldestEdge
                    : ReportQueueResult::Appended;
}

} // namespace opennow::input
