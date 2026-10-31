#include "stream_maintenance_policy.hpp"
#include <cassert>
int main(){
    using namespace opennow::streaming;
    assert(!LiveMaintenanceHandoff(false,false));
    assert(!LiveMaintenanceHandoff(true,false)); // ICE alone does not change startup ownership.
    assert(!LiveMaintenanceHandoff(false,true));
    assert(LiveMaintenanceHandoff(true,true));
    assert(MaintenanceDue(1000000,0));
    assert(!MaintenanceDue(1099999,1000000));
    assert(MaintenanceDue(1100000,1000000));
    assert(MaintenanceDue(900000,1000000));
    unsigned calls=0;
    unsigned long long last=0;
    for(unsigned long long t=1000000;t<2000000;t+=1000)
        if(MaintenanceDue(t,last)){last=t;++calls;}
    assert(calls==10); // Live signaling is bounded to 10 Hz, not every UI frame.

    assert(!DeferLiveMaintenance(1100000,1000000,false,false));
    assert(!DeferLiveMaintenance(1100000,1000000,true,true)); // Recovery never waits for input.
    assert(!DeferLiveMaintenance(1100000,0,true,false)); // Initial live handoff is immediate.
    assert(!DeferLiveMaintenance(1099999,1000000,true,false));
    assert(DeferLiveMaintenance(1100000,1000000,true,false));
    assert(DeferLiveMaintenance(1104999,1000000,true,false));
    assert(!DeferLiveMaintenance(1105000,1000000,true,false));
    assert(!DeferLiveMaintenance(900000,1000000,true,false)); // Clock reversal cannot defer.

    // A report queued at the due instant is serviced on the next 1ms pass,
    // before a simulated 18ms signaling operation. Ordinary cadence resumes
    // as soon as the queue empties; no fixed delay is added to every poll.
    const unsigned long long due=1100000;
    unsigned long long input_sent=0, maintenance_at=0;
    for(auto now=due;now<=due+5000;now+=1000) {
        if(now==due+1000) input_sent=now;
        if(MaintenanceDue(now,1000000) &&
            !DeferLiveMaintenance(now,1000000,input_sent==0,false)) {
            maintenance_at=now; break;
        }
    }
    assert(input_sent==due+1000 && maintenance_at==input_sent);
    assert(input_sent<due+18000); // Report no longer follows the signaling call.

    // Continuous input still services signaling at most 5ms beyond its due
    // time. The deadline cannot slide forward on each deferral.
    last=1000000; calls=0;
    for(unsigned long long now=1000001;now<=2050000;++now) {
        if(MaintenanceDue(now,last) && !DeferLiveMaintenance(now,last,true,false)) {
            assert(now-last==105000);
            last=now; ++calls;
        }
    }
    assert(calls==10);
}
