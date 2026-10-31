#include "stream/ReadyFrameLatencyPolicy.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <deque>
using opennow::video::ReadyFrameLatencyPolicy;
int main() {
    ReadyFrameLatencyPolicy policy;
    unsigned trims=0; std::uint64_t trimmed_at=0;
    for(std::uint64_t t=1000000;t<=5100000;t+=16667)
        if(policy.Consider(t,16667,2500,16667,3,36000,19000,1500,false)){++trims;trimmed_at=t;}
    assert(trims==1);
    assert(policy.Protect(trimmed_at+100000));
    for(std::uint64_t t=trimmed_at+100001;t<trimmed_at+30100000;t+=16667)
        assert(!policy.Consider(t,16667,2500,16667,3,36000,19000,1500,false));
    // Further repeats cannot shorten the protective 30s interval.
    assert(!policy.Protect(trimmed_at+1000000));
    assert(!policy.Consider(trimmed_at+10000000,16667,2500,16667,3,36000,19000,1500,false));
    for(auto period:{33333,50000}) {
        ReadyFrameLatencyPolicy slow;
        for(std::uint64_t t=1000000;t<10000000;t+=33333)
            assert(!slow.Consider(t,period,1000,period,3,50000,20000,3000,false));
    }
    for(int scenario=0;scenario<6;++scenario){
        ReadyFrameLatencyPolicy unsafe;
        for(std::uint64_t t=1000000;t<10000000;t+=16667)
            assert(!unsafe.Consider(t,16667,scenario==0?9000:2500,
                scenario==1?30000:16667,scenario==2?2:3,36000,
                scenario==3?29000:19000,scenario==4?3000:1500,scenario==5));
    }
    ReadyFrameLatencyPolicy reversed;
    assert(!reversed.Consider(5000000,16667,1000,16667,3,36000,19000,1500,false));
    assert(!reversed.Consider(1000000,16667,1000,16667,3,36000,19000,1500,false));
    // R41 trimmed even a two-picture queue. R42 keeps its jitter reserve.
    ReadyFrameLatencyPolicy shallow;
    for(std::uint64_t t=1000000;t<20000000;t+=16667)
        assert(!shallow.Consider(t,16667,1000,16667,2,36000,19000,1500,false));
    ReadyFrameLatencyPolicy replay;
    std::deque<std::uint64_t> arrived;
    std::uint64_t producer=1000000, age_total=0;
    unsigned replay_trims=0,repeats=0;
    for(std::uint64_t display=1035000;display<11035000;display+=16667){
        while(producer<=display){arrived.push_back(producer);producer+=16667;}
        if(arrived.empty()){++repeats;replay.Protect(display);continue;}
        if(replay.Consider(display,16667,1500,16667,arrived.size(),display-arrived.front(),
            arrived.size()>1?display-arrived[1]:0,1500,false)){arrived.pop_front();++replay_trims;}
        assert(!arrived.empty());age_total+=display-arrived.front();arrived.pop_front();
    }
    assert(replay_trims==1 && repeats==0 && age_total/600<27000);
}
