#include "stream/audio/AudioServiceDeadline.hpp"
#include <cassert>

int main() {
    using opennow::audio::ServiceDeadline;
    ServiceDeadline deadline(100000);
    for (unsigned elapsed = 0; elapsed < 5000; elapsed += 100) {
        assert(!deadline.ShouldService(100000 + elapsed, true, false));
        assert(deadline.WaitUs(100000 + elapsed, 12000) == 5000 - elapsed);
    }
    assert(deadline.ShouldService(105000, true, false));
    assert(deadline.WaitUs(106000, 12000) == 0);
    assert(deadline.ShouldService(100001, true, true));
    assert(deadline.ShouldService(100001, false, false));
    // Reordering grace survives multiple output service turns; no false PLC.
    ServiceDeadline second(105000);
    assert(second.WaitUs(107000, 9000) == 3000);
    assert(second.WaitUs(107000, 1000) == 1000);
}
