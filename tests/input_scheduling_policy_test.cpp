#include "input_scheduling_policy.hpp"
#include <cassert>
int main() {
    using namespace opennow::input;
    assert(!DeferUiMaintenance(false,true,100000,90000));
    assert(!DeferUiMaintenance(true,false,100000,90000));
    assert(!DeferUiMaintenance(true,true,100000,0));
    assert(DeferUiMaintenance(true,true,149999,100000));
    assert(!DeferUiMaintenance(true,true,150000,100000));
    assert(!DeferUiMaintenance(true,true,100000,100001));
    assert(HeartbeatDue(1,0,true));
    assert(!HeartbeatDue(2999999,1000000,false));
    assert(HeartbeatDue(3000000,1000000,false));
    assert(!HeartbeatDue(3000000,1000000,true));
    assert(!HeartbeatDue(3099999,1000000,true));
    assert(HeartbeatDue(3100000,1000000,true)); // no starvation under continuous input
    assert(!EndInputBatch(0,85000));
    assert(!EndInputBatch(1,1999));
    assert(EndInputBatch(1,2000));
    assert(EndInputBatch(1,85000));
}
