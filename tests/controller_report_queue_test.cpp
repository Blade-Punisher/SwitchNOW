#include "controller_report_queue.hpp"

#include <cassert>
#include <cstdint>
#include <deque>

namespace
{
struct Report
{
    std::uint32_t buttons;
    int axis;
};
}

int main()
{
    using opennow::input::QueueControllerReport;
    using opennow::input::ReportQueueResult;
    std::deque<Report> reports;
    assert(QueueControllerReport(reports, Report {0, 1}, 3) ==
           ReportQueueResult::Appended);
    assert(QueueControllerReport(reports, Report {0, 2}, 3) ==
           ReportQueueResult::CoalescedAxes);
    assert(reports.size() == 1 && reports.front().axis == 2);
    assert(QueueControllerReport(reports, Report {1, 3}, 3) ==
           ReportQueueResult::Appended);
    assert(QueueControllerReport(reports, Report {0, 4}, 3) ==
           ReportQueueResult::Appended);
    assert(reports.size() == 3 && reports[0].buttons == 0 &&
           reports[1].buttons == 1 && reports[2].buttons == 0);
    assert(QueueControllerReport(reports, Report {2, 5}, 3) ==
           ReportQueueResult::DroppedOldestEdge);
    assert(reports.size() == 3 && reports.front().buttons == 1);
    reports.clear();
    using opennow::input::ControllerEdgeState;
    QueueControllerReport(reports, Report {ControllerEdgeState(0,0,0),0},8);
    QueueControllerReport(reports, Report {ControllerEdgeState(0,255,0),1},8);
    QueueControllerReport(reports, Report {ControllerEdgeState(0,128,0),2},8);
    assert(reports.size() == 2 && reports.back().axis == 2); // latest pressure while held
    QueueControllerReport(reports, Report {ControllerEdgeState(0,0,0),3},8);
    assert(reports.size() == 3); // short trigger press and release stay ordered
    QueueControllerReport(reports, Report {ControllerEdgeState(0,0,255),4},8);
    QueueControllerReport(reports, Report {ControllerEdgeState(0,0,0),5},8);
    assert(reports.size() == 5);
    assert(ControllerEdgeState(0xffff,0,0) != ControllerEdgeState(0xffff,255,0));
    return 0;
}
