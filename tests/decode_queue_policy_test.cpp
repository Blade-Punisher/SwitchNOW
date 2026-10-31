#include "stream/DecodeQueuePolicy.hpp"

#include <cassert>

static_assert(opennow::video::MaximumQueuedAccessUnits(30) == 3);
static_assert(opennow::video::MaximumQueuedAccessUnits(60) == 5);
static_assert(opennow::video::MaximumQueuedAccessUnits(120) == 6);
static_assert(opennow::video::ReadyFrameQueueCapacity() == 3);
static_assert(opennow::video::StartupReadyFrameDepth() == 2);
static_assert(opennow::video::MaximumReadyFrameAgeMs() == 50);
static_assert(opennow::video::FrameWaitBudgetMicroseconds(60) == 6500);
static_assert(opennow::video::MaximumDecodeQueueDelayMs() == 67);
static_assert(opennow::video::MaximumDecodeUnitBytes() == 2 * 1024 * 1024);

int main()
{
    assert(opennow::video::MaximumQueuedAccessUnits(1) == 2);
    assert(opennow::video::MaximumQueuedAccessUnits(60) < 8);
    assert(opennow::video::FrameWaitBudgetMicroseconds(30) == 6500);
    assert(opennow::video::FrameWaitBudgetMicroseconds(120) == 3333);
    assert(opennow::video::FrameWaitBudgetMicroseconds(0) == 6500);
    assert(opennow::video::FrameWaitBudgetMicroseconds(1000) == 2500);
    using opennow::video::DecodeQueueNeedsResync;
    assert(!DecodeQueueNeedsResync(60, 5, 1000));
    assert(!DecodeQueueNeedsResync(60, 7, 16000));
    assert(DecodeQueueNeedsResync(60, 8, 1000));
    assert(DecodeQueueNeedsResync(60, 5, 17000));
    assert(!DecodeQueueNeedsResync(60, 4, 66000));
    assert(DecodeQueueNeedsResync(60, 1, 67000));
    assert(!DecodeQueueNeedsResync(60, 0, 67000));
    assert(!DecodeQueueNeedsResync(30, 5, 32000));
    assert(DecodeQueueNeedsResync(30, 6, 1000));
    assert(DecodeQueueNeedsResync(0, 2, 67000));
    return 0;
}
