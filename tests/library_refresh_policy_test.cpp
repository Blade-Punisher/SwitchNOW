#include "library_refresh_policy.hpp"
#include "game_feed_policy.hpp"
#include "http_cancellation.hpp"
#include <cassert>
#include <iostream>
using namespace opennow;
using namespace game_feed;
using namespace std::chrono;
int main() {
    auto now=RefreshSchedule::Clock::time_point{}+seconds(1);
    RefreshSchedule s;assert(s.Due(now));s.Begin(now,false);assert(!s.Due(now));
    assert(!s.Expired(now+seconds(99)));assert(s.Expired(now+seconds(100)));
    // Return to the menu can precede the worker's launch-cancellation callback.
    // Completion schedules recovery independently of another willAppear event.
    Requests jobs;auto old=jobs.Begin();LaunchGate launch;
    assert(launch.Check(true));assert(launch.Check(false));
    s.Deferred(now);assert(!s.Due(now+milliseconds(499)));assert(s.Due(now+milliseconds(500)));
    auto next=jobs.Begin();s.Begin(now+seconds(1),false);assert(!jobs.Current(old)&&jobs.Current(next));
    // A late reply for a timed-out generation cannot finish its replacement.
    jobs.Invalidate();s.Failed(now);auto replacement=jobs.Begin();assert(!jobs.Current(next)&&jobs.Current(replacement));
    assert(!s.Due(now+seconds(1)));assert(s.Due(now+seconds(2)));
    s.Begin(now+seconds(2),false);s.Failed(now+seconds(2));assert(!s.Due(now+seconds(9)));assert(s.Due(now+seconds(10)));
    s.Begin(now+seconds(10),false);s.Failed(now+seconds(10));assert(!s.Due(now+seconds(39)));assert(s.Due(now+seconds(40)));
    s.Begin(now+seconds(40),false);s.Failed(now+seconds(40));assert(!s.Due(now+hours(1))); // bounded, no retry storm
    s.Begin(now+hours(1),true);s.Failed(now+hours(1));assert(s.Due(now+hours(1)+seconds(2))); // manual retry rearms
    s.Complete(now);assert(!s.Due(now+seconds(119)));assert(s.Due(now+seconds(120)));
    s.Reset();assert(s.Due(now)); // changed account/revision must not inherit backoff
    bool expired=false;
    {HttpCancellationScope parent([&]{return expired;});HttpCancellationScope launch_scope([]{return false;});auto cancel=HttpCancellationScope::Callback();assert(!cancel());expired=true;assert(cancel());}
    assert(!HttpCancellationScope::Callback());
    std::cout<<"Library return-before-deferred recovery, bounded 2/8/30 retries, watchdog generation isolation, manual/account reset and nested deadline cancellation passed.\n";
}
