#include "stream/audio/AudioGainPolicy.hpp"
#include <cassert>
#include <cmath>
int main() {
    using opennow::audio::GainEnvelope;
    GainEnvelope gain {6,0};
    const auto attenuated = gain.Update(20000,6,480);
    assert(std::abs(attenuated - 1.5f) < .0001f);
    for (unsigned i=0;i<20;++i) assert(gain.Update(1000,6,480) == attenuated);
    float previous = gain.gain;
    for (unsigned i=0;i<100;++i) {
        const auto next = gain.Update(1000,6,480);
        assert(next >= previous && next <= 6);
        assert(next - previous < .1f);
        previous = next;
    }
    for (unsigned i=0;i<1000;++i) {
        const int peak = i%17==0 ? 32768 : i%7==0 ? 12000 : 1000;
        const auto current = gain.Update(peak,6,480);
        assert(current * peak <= 30000.01f && std::isfinite(current));
    }
    GainEnvelope unity {1,0};
    assert(unity.Update(32768,1,480) == 1); // Native volume remains native.
    GainEnvelope variable {6,0};
    variable.Update(20000,6,960);
    for (unsigned i=0;i<10;++i) assert(variable.Update(1000,6,960) == 1.5f);
}
