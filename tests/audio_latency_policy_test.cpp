#include "stream/audio/AudioLatencyPolicy.hpp"

#include <cassert>

static_assert(opennow::audio::ClampTargetBufferMs(5) == 30);
static_assert(opennow::audio::ClampTargetBufferMs(140) == 100);
static_assert(opennow::audio::PrimePacketCount(40) == 4);
static_assert(opennow::audio::ClampVolumeBoostPercent(50) == 100);
static_assert(opennow::audio::ClampVolumeBoostPercent(1200) == 800);
static_assert(opennow::audio::OutputQueueLimit(40) == 11);
static_assert(opennow::audio::MaximumAdaptiveStartBuffers(40) == 9);
static_assert(opennow::audio::InitialJitterHoldUs(40) == 40000);
static_assert(opennow::audio::ResyncJitterHoldUs(40) == 30000);
static_assert(opennow::audio::PacketReorderGraceUs() == 4000);
static_assert(opennow::audio::EstimatePlayedSamples(100, 1000, 11000, 1000, 48000) == 580);
static_assert(opennow::audio::PlayedSampleDelta(1000, 1480) == 480);
static_assert(opennow::audio::PlayedSampleDelta(431728, 13200) == 13200);

int main()
{
    using opennow::audio::RecoveryStartBuffers;
    assert(RecoveryStartBuffers(4,6,1,40,40) == 5);
    assert(RecoveryStartBuffers(4,6,1,0,40) == 4);
    assert(RecoveryStartBuffers(4,6,2,0,40) == 5);
    assert(RecoveryStartBuffers(6,6,20,80,40) == 6);
    assert(RecoveryStartBuffers(4,6,20,500,40) == 4);
    assert(opennow::audio::RecentUnderrun(28000000, 1000000)); // 27s gap in the supplied log
    assert(!opennow::audio::RecentUnderrun(62000000, 1000000));
    assert(!opennow::audio::RecentUnderrun(1000000, 2000000));
    assert(!opennow::audio::RecentUnderrun(1000000, 0));
    assert(!opennow::audio::CanReducePrebuffer(30999999, 1000000));
    assert(opennow::audio::CanReducePrebuffer(61000000, 1000000));
    assert(opennow::audio::InitialJitterHoldUs(30) <
           opennow::audio::InitialJitterHoldUs(100));
    assert(opennow::audio::ResyncJitterHoldUs(100) == 50000);
    assert(opennow::audio::EstimatePlayedSamples(900, 1000, 11000, 1000, 48000) == 1000);
    assert(opennow::audio::OutputQueueLimit(100) == 14);
    assert(opennow::audio::MaximumAdaptiveStartBuffers(100) == 10);
    return 0;
}
