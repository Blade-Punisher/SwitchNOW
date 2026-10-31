#pragma once
#include <array>
#include <chrono>
namespace opennow::game_feed {
// UI-thread schedule. Worker cancellation uses its captured steady deadline;
// timed-out generations are invalidated before another refresh may start.
class RefreshSchedule {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr auto kBudget=std::chrono::seconds(100);
    void Reset(){*this=RefreshSchedule{};}
    void Begin(Clock::time_point now,bool manual){if(manual)retries_=0;deadline_=now+kBudget;pending_=false;}
    bool Expired(Clock::time_point now)const{return now>=deadline_;}
    void Complete(Clock::time_point now){retries_=0;pending_=true;next_=now+std::chrono::minutes(2);}
    void Deferred(Clock::time_point now){pending_=true;next_=now+std::chrono::milliseconds(500);}
    void Failed(Clock::time_point now){
        constexpr std::array<int,3> delays{2,8,30};
        pending_=retries_<delays.size();
        if(pending_)next_=now+std::chrono::seconds(delays[retries_++]);
    }
    bool Due(Clock::time_point now)const{return pending_&&now>=next_;}
private:
    unsigned retries_=0;
    bool pending_=true;
    Clock::time_point deadline_{},next_{};
};
}
