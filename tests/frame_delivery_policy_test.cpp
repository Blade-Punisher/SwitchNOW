#include "frame_delivery_policy.hpp"
#include <cassert>
int main() {
    using opennow::streaming::FrameDelivery;
    FrameDelivery regular;
    for (int i=0;i<1000;++i) regular.Observe(16667,1500,1500);
    assert(regular.P95() == 16667 && regular.jitter_us < 10 && !regular.over33);
    FrameDelivery burst;
    for (int i=0;i<300;++i) burst.Observe(i%2 ? 3000 : 30334,1500,1500);
    assert(burst.jitter_us > 12000 && burst.P95() == 33334);
    auto before = burst.jitter_us;
    burst.Observe(250000,22500,1500);
    assert(burst.jitter_us == before && burst.over100 == 1);
    burst.Observe(33334,3000,1500); // actual 30 FPS source cadence does not inflate jitter
    assert(burst.jitter_us == before);
    const auto samples = burst.samples;
    burst.Observe(1,0,1500); burst.Observe(10000,-1500,1500);
    assert(burst.samples == samples);
}
