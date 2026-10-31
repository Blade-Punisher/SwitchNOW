#include "ingress_timing_policy.hpp"
#include <cassert>
int main() {
    using opennow::streaming::IngressTiming;
    IngressTiming t;
    assert(t.Poll(1000000) == 0);
    assert(t.Poll(1001000) == 1000);
    t.Raw(1,1001000); t.Raw(2,1002000);
    assert(t.Unit(1003000) == 2000);
    assert(t.Unit(1004000) == 0); // no fabricated first-packet time
    t.Raw(2,1005000); assert(t.last_raw_us == 1002000);
    assert(IngressTiming::Age(1100000,t.last_raw_us) == 98000);
    assert(t.Poll(2000000) == 999000); // stalled worker, not proof of network delay
    assert(IngressTiming::Age(1,0) == 0 && IngressTiming::Age(1,2) == 0);
    t.Raw(0xffffffffu,2000000); t.Raw(0,2001000);
    assert(t.last_raw_us == 2001000); // raw counter wrap
}
