#include "signaling_registration_policy.hpp"
#include <cassert>
using opennow::signaling::RegistrationPolicy;
int main() {
    RegistrationPolicy browser(true);
    assert(!browser.ShouldSend(true,false,0) && !browser.Confirmed());
    browser.Sent(1,true,0);assert(browser.Attempts()==0);
    browser.Identity();assert(browser.Confirmed() && !browser.AckSeen());
    assert(!browser.ShouldSend(true,false,60000000));
    RegistrationPolicy p;
    assert(p.ShouldSend(true,false,0));
    assert(!p.ShouldSend(false,false,0) && !p.ShouldSend(true,true,0));
    p.Sent(11,true,0);
    assert(!p.ShouldSend(true,false,1999999));
    assert(p.ShouldSend(true,false,2000000));
    p.Identity();p.Ack(99);assert(!p.Confirmed());
    p.Ack(11);assert(p.Confirmed());
    for(unsigned long long now=2000000;now<60000000;now+=1000)
        assert(!p.ShouldSend(true,false,now));
    RegistrationPolicy delayed;
    delayed.Sent(21,true,100);delayed.Sent(22,true,2000100);
    delayed.Ack(21);assert(!delayed.Confirmed());
    delayed.Identity();assert(delayed.Confirmed()); // Late ACK survives a retry.
    assert(!delayed.ShouldSend(true,false,4000100));
    RegistrationPolicy failed;
    failed.Ack(0);assert(!failed.AckSeen()); // Unused slots are never successful requests.
    failed.Sent(31,false,1000000);failed.Identity();failed.Ack(31);
    assert(!failed.Confirmed() && !failed.AckSeen());
    assert(!failed.ShouldSend(true,false,999999)); // Clock reversal cannot spam sends.
    assert(failed.ShouldSend(true,false,3000000));
    failed.Sent(32,true,3000000);failed.Ack(32);assert(failed.Confirmed());
    RegistrationPolicy bounded;
    for(unsigned i=0;i<RegistrationPolicy::MaximumAttempts;++i) {
        const auto now=i*2000000ULL;
        assert(bounded.ShouldSend(true,false,now));bounded.Sent(i+1,true,now);
    }
    assert(bounded.Attempts()==3 && !bounded.ShouldSend(true,false,6000000));
    bounded.Sent(4,true,6000000);bounded.Identity();bounded.Ack(4);
    assert(bounded.Attempts()==3 && !bounded.Confirmed());
    bounded.Ack(1);assert(bounded.Confirmed());
    RegistrationPolicy new_socket;
    assert(!new_socket.Confirmed() && new_socket.ShouldSend(true,false,0));
}
