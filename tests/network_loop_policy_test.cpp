#include "network_loop_policy.hpp"

#include <cassert>

static_assert(opennow::network::LoopBackoffMilliseconds(false, 0) == 2);
static_assert(opennow::network::LoopBackoffMilliseconds(true, 0) == 1);
static_assert(opennow::network::LoopBackoffMilliseconds(true, 1) == 0);

int main()
{
    using opennow::network::LoopBackoffMilliseconds;
    assert(LoopBackoffMilliseconds(false, 0) >
           LoopBackoffMilliseconds(true, 0));
    assert(LoopBackoffMilliseconds(true, 8) == 0);
    using opennow::network::QueuedInputCanRun;
    assert(QueuedInputCanRun(true,true,true,100000,0));
    assert(!QueuedInputCanRun(false,true,true,100000,0));
    assert(!QueuedInputCanRun(true,false,true,100000,0));
    assert(!QueuedInputCanRun(true,true,false,100000,0));
    assert(!QueuedInputCanRun(true,true,true,114999,115000));
    assert(QueuedInputCanRun(true,true,true,115000,115000));
    assert(!QueuedInputCanRun(true,true,true,0,115000));
    // An old report has no new generation to wake an idle wait. It is still
    // ready now, while a backpressured queue must retain every normal wait.
    unsigned retry_passes=0;
    for(unsigned now=100000;now<=115000;now+=1000) {
        if(QueuedInputCanRun(true,true,true,now,115000)) ++retry_passes;
    }
    assert(retry_passes==1); // no hot spin / early send during the 15ms hold
    return 0;
}
