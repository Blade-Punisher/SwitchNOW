#include "stream/audio/AudioRecoveryPolicy.hpp"
#include <cassert>
int main() {
    using namespace opennow::audio;
    ArrivalJitter stable;
    for (unsigned i=0;i<1000;++i) stable.Observe(i, i*480, 1000000+i*10000);
    assert(stable.jitter_us == 0);
    ArrivalJitter burst;
    for (unsigned i=0;i<100;++i)
        burst.Observe(i, i*480, 1000000+i*10000+(i%2?9000:0));
    assert(burst.jitter_us >= 8000 && burst.jitter_us <= 9000);
    const auto before = burst.jitter_us;
    burst.Observe(98,98*480,3000000); // Late/duplicate packets do not train timing.
    assert(burst.jitter_us == before && burst.sequence == 99);
    burst.Observe(100,100*480,4000000); // A long outage is not normal jitter.
    assert(burst.jitter_us == before);
    ArrivalJitter wrap;
    wrap.Observe(65535,0xfffffe20,1000000);
    wrap.Observe(0,0,1010000);
    assert(wrap.jitter_us == 0 && wrap.sequence == 0);
    assert(RecoveryReserve(4,4,7,1,0,0) == 4);
    assert(RecoveryReserve(4,4,7,1,9000,30) == 5);
    assert(RecoveryReserve(6,4,7,3,16000,40) == 7);
    assert(RecoveryReserve(4,4,7,20,30000,500) == 4);
    int reserve=4;
    for (unsigned i=1;i<=50;++i) reserve=RecoveryReserve(reserve,4,7,i,5750,60);
    assert(reserve == 5); // R30 log: repeated outages must not force 70ms at 5.75ms jitter.
    assert(RecoveryReserve(4,4,7,2,11000,60) == 6);
    assert(!CanRestartOutput(4,7,4,1100000,1000000));
    assert(CanRestartOutput(4,7,4,1150000,1000000));
    assert(!CanRestartOutput(3,7,4,2000000,1000000));
    assert(!CanRestartOutput(4,7,4,2000000,0)); // Startup keeps its configured reserve.
    assert(CanRestartOutput(7,7,4,1000000,0));
    ShortGapReserve short_gaps;
    for (unsigned i=0;i<3;++i) short_gaps.Observe(1000000+i*1000000,60,4);
    assert(short_gaps.Demand(3000000,4,7,5) == 6);
    assert(short_gaps.Demand(13000000,4,7,5) == 5);
    assert(short_gaps.Demand(2000000,4,7,5) == 5); // clock reversal
    ShortGapReserve outages;
    for (unsigned i=0;i<20;++i) outages.Observe(1000000+i*1000000,500,4);
    assert(outages.Demand(20000000,4,7,5) == 5);
    ShortGapReserve occasional;
    for (unsigned i=0;i<20;++i) occasional.Observe(1000000+i*20000000ULL,60,4);
    assert(occasional.Demand(381000000,4,7,5) == 5);
    ReserveRelease release;
    int current = 7;
    // Replay moderate jitter with repeated short underruns every 10 seconds.
    // They need 50ms, not a permanent 70ms reserve from an earlier burst.
    for (unsigned i = 0; i <= 6; ++i) {
        const auto demand = RecoveryReserve(4,4,7,30+i,5750,47);
        current = release.Observe(1000000 + i*10000000ULL, current, demand);
    }
    assert(current == 5);
    release = {};
    assert(release.Observe(1000000,7,5) == 7);
    assert(release.Observe(30999999,7,5) == 7);
    assert(release.Observe(31000000,7,7) == 7); // renewed high jitter cancels release
    assert(release.Observe(32000000,7,5) == 7);
    assert(release.Observe(62000000,7,5) == 6);
    assert(release.Observe(62000001,6,5) == 6);

    ShortGapReserve frequent;
    for (unsigned i=0;i<8;++i) frequent.Observe(1000000+i*1000000,77,4);
    assert(frequent.Demand(8000000,4,9,5) == 9);
    assert(frequent.Demand(18000000,4,9,5) == 5);
    EmptyQueueGrace grace;
    assert(!grace.MustReprime(1000000));
    assert(!grace.MustReprime(1019999));
    grace.Filled(1010000); // new PCM resumes without the 70-90ms re-prime
    assert(grace.resumed == 1 && grace.silence_us == 10000 && grace.began_us == 0);
    assert(!grace.MustReprime(2000000));
    assert(grace.MustReprime(2020000));
    grace.Reprimed();
    grace.Filled(2100000);
    assert(grace.resumed == 1); // a full stop is not counted as a soft resume
    assert(!grace.MustReprime(3000000));
    assert(!grace.MustReprime(2999999)); // clock discontinuity restarts the grace
    assert(EmptyGraceBudgetUs(9000,60,0)==20000);
    assert(EmptyGraceBudgetUs(9000,60,2)==38000);
    assert(EmptyGraceBudgetUs(1000000,60,8)==40000);
    assert(EmptyGraceBudgetUs(9000,200,8)==20000);
    // Delayed PCM at 32ms resumes directly after recurrent moderate jitter;
    // the previous 20ms rule had already forced a full stop/re-prime.
    EmptyQueueGrace adaptive, old;
    const auto budget=EmptyGraceBudgetUs(8000,55,3);
    assert(!adaptive.MustReprime(1000000,budget));
    assert(!old.MustReprime(1000000));
    assert(old.MustReprime(1021000));
    assert(!adaptive.MustReprime(1032000,budget));
    adaptive.Filled(1032000);
    assert(adaptive.resumed==1 && adaptive.silence_us==32000);
    assert(!adaptive.MustReprime(2000000,40000));
    assert(adaptive.MustReprime(2040000,40000)); // Never wait indefinitely.
    adaptive.Reprimed();

}
