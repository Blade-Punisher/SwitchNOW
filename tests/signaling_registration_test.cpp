#include "signaling_registration.hpp"
#include <jansson.h>
#include <cassert>
using namespace opennow::signaling;
static json_t* Parse(const char* s) { json_error_t e{};return json_loads(s,0,&e); }
int main() {
    RegistrationPolicy p;p.Sent(1,true,219000);
    auto* identity=Parse(R"({"peer_info":{"name":"expected","id":5,"connected":true},"ackid":9})");
    RegistrationPolicy browser(true);
    assert(!ObserveRegistration(browser,identity,"wrong-peer"));
    assert(ObserveRegistration(browser,identity,"expected") && browser.Confirmed());
    assert(!ObserveRegistration(browser,identity,"expected"));
    assert(IncomingAckId(identity)==9); // Own peer_info must be acknowledged.
    auto* ack=Parse(R"({"ack":1})");
    assert(!ObserveRegistration(p,identity,"expected"));
    assert(ObserveRegistration(p,ack,"expected"));
    assert(!ObserveRegistration(p,ack,"expected")); // Confirmation event is emitted once.
    assert(!IncomingAckId(ack)); // An ACK never causes an ACK loop.
    assert(!IncomingAckId(nullptr));
    for(const auto* text : {R"({"ackid":-1})", R"({"ackid":"9"})", R"({"ackid":null})", R"({"hb":1})"}) {
        auto* invalid_ack=Parse(text);assert(!IncomingAckId(invalid_ack));json_decref(invalid_ack);
    }
    auto* hb=Parse(R"({"hb":1})");
    for(unsigned long long now=2000000;now<30000000;now+=1000) {
        assert(!p.ShouldSend(true,false,now));
        if(now%2000000==0) assert(!ObserveRegistration(p,hb,"expected"));
    }
    assert(p.Attempts()==1 && p.Confirmed()); // R45-shaped trace no longer sends 15 registrations.
    RegistrationPolicy reverse;reverse.Sent(1,true,0);
    assert(!ObserveRegistration(reverse,ack,"expected"));
    assert(ObserveRegistration(reverse,identity,"expected"));
    RegistrationPolicy retry;retry.Sent(1,true,0);retry.Sent(2,true,2000000);
    assert(!ObserveRegistration(retry,identity,"expected"));
    assert(ObserveRegistration(retry,ack,"expected")); // Earlier successful write may ACK late.
    RegistrationPolicy invalid;invalid.Sent(1,true,0);
    assert(!ObserveRegistration(invalid,identity,"different"));
    assert(!ObserveRegistration(invalid,ack,"different") && !invalid.Confirmed());
    auto* wrong=Parse(R"({"peer_info":{"name":"expected","id":"5"},"ack":"1"})");
    assert(!ObserveRegistration(invalid,wrong,"expected") && !invalid.IdentitySeen());
    assert(!ObserveRegistration(invalid,nullptr,"expected"));
    json_decref(wrong);json_decref(hb);json_decref(ack);json_decref(identity);
}
