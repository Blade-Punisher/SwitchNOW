#include "../app/src/stream/ffmpeg/VideoFrameTiming.hpp"

#include <cassert>

int main()
{
    using namespace opennow::video;

    assert(DecideFrame(90000, false, 0, true, 90000) == FrameDecision::Present);
    assert(DecideFrame(91500, false, 0, true, 90000) == FrameDecision::HoldPrevious);
    assert(DecideFrame(90000, true, 90600, true, 90000) == FrameDecision::DropSuperseded);
    assert(DecideFrame(90000, true, 91500, true, 90000) == FrameDecision::Present);
    assert(DecideFrame(91500, false, 0, false, 90000) == FrameDecision::Present);

    assert(AdaptiveEarlyToleranceTicks(0) == 180);
    assert(AdaptiveEarlyToleranceTicks(1500) == 150);
    assert(AdaptiveEarlyToleranceTicks(1800) == 180);
    assert(AdaptiveEarlyToleranceTicks(3000) == 300);
    assert(RecoveryEarlyToleranceTicks(1500) == 500);
    assert(RecoveryEarlyToleranceTicks(3000) == 600);

    const FrameTimingPolicy source_locked {180};
    const FrameTimingPolicy recovery {500};
    assert(DecideFrame(90300, false, 0, true, 90000, source_locked) ==
           FrameDecision::HoldPrevious);
    assert(DecideFrame(90300, false, 0, true, 90000, recovery) ==
           FrameDecision::Present);
    assert(DecideFrame(90000, true, 90600, true, 90000, source_locked) ==
           FrameDecision::Present);

    // RTP wraparound must retain signed timestamp ordering.
    assert(RtpDelta(0x00000100u, 0xfffffff0u) > 0);
    assert(RtpDelta(0xfffffff0u, 0x00000100u) < 0);
    assert(!ShouldResyncForDamagedFrame(90000, 91500, true));
    assert(ShouldResyncForDamagedFrame(93000, 91500, true));
    assert(ShouldResyncForDamagedFrame(90000, 0, false));
    assert(!ShouldResyncForDamagedFrame(0xfffffff0u, 0x00000100u, true));
    assert(WaitBudgetAfterMisses(6500, 2) == 6500);
    assert(WaitBudgetAfterMisses(6500, 3) == 1500);
    assert(ClassifyFrameArrival(1500, 16667, 1500) == FrameArrivalKind::Regular);
    assert(ClassifyFrameArrival(3000, 33334, 1500) == FrameArrivalKind::TimestampGap);
    assert(ClassifyFrameArrival(1500, 32000, 1500) == FrameArrivalKind::DeliveryGap);
    assert(ClassifyFrameArrival(1500, 3000, 1500) == FrameArrivalKind::Burst);
    assert(PredictFrameWaitUs(4500, 0, 16667, 1000, 15000) > 3000);
    assert(PredictFrameWaitUs(6500, 3, 16667, 9000, 16000) == 1500);
    assert(PredictFrameWaitUs(4500, 0, 16667, 2000, 60000) == 0);
    assert(PredictFrameWaitUs(4500, 0, 16667, 2000, 1000) == 0);
    assert(PredictFrameWaitUs(0, 0, 16667, 2000, 16000) == 0);
    return 0;
}
