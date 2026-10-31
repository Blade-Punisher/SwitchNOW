#include "stream_status_policy.hpp"
#include <cassert>
int main() {
    using namespace opennow::status;
    assert(ConnectionStage(false, false, false, false, 0, 0) == Stage::Signaling);
    assert(ConnectionStage(true, false, false, false, 0, 0) == Stage::Transport);
    assert(ConnectionStage(true, true, false, false, 0, 0) == Stage::Input);
    assert(ConnectionStage(true, true, true, false, 0, 0) == Stage::Video);
    assert(ConnectionStage(true, true, false, true, 1, 0) == Stage::Decode);
    assert(ConnectionStage(true, true, true, true, 1, 1) == Stage::Ready);
    Sample sample {60, 60, 60, 60};
    assert(Evaluate(sample) == Bottleneck::Stable);
    sample.gaps = 1;
    assert(Evaluate(sample) == Bottleneck::Stable); // Recovered gap at 60 FPS is informational.
    sample.displayed = 40;
    assert(Evaluate(sample) == Bottleneck::Loss);
    sample.gaps = 0; sample.incoming = sample.decoded = sample.displayed = 30;
    assert(Evaluate(sample) == Bottleneck::Incoming); // Cannot blame server alone.
    sample.target = 30;
    assert(Evaluate(sample) == Bottleneck::Stable);
    sample = {60, 35, 35, 60, 0, 0, 0, 25000, 4};
    assert(Evaluate(sample) == Bottleneck::Decode);
    sample = {60, 60, 35, 60};
    assert(Evaluate(sample) == Bottleneck::Presentation);
    sample = {30,30,30,60,0,50,0};
    assert(Evaluate(sample) == Bottleneck::Incoming); // late RTX does not explain away missing source frames
    sample = {53,53,53,60,0,50,0};
    assert(Evaluate(sample) == Bottleneck::Delivery);
    Assessment assessment;
    assert(!assessment.Observe(Bottleneck::Loss));
    assert(!assessment.Observe(Bottleneck::Stable));
    assert(assessment.current == Bottleneck::Measuring);
    assert(assessment.Observe(Bottleneck::Stable));
    assert(!assessment.Observe(Bottleneck::Stable));
    assert(!assessment.Observe(Bottleneck::Incoming));
    assert(assessment.Observe(Bottleneck::Incoming));
    assert(assessment.current == Bottleneck::Incoming);
}
