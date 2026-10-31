#pragma once
#include <array>
#include <cstdint>
namespace opennow::signaling {
// Per WebSocket attempt; caller serializes state with signaling I/O. It is
// independent from diagnostic settings and keeps all successful request IDs
// until the bounded retry budget ends, so delayed ACKs are not lost.
class RegistrationPolicy {
public:
    static constexpr unsigned MaximumAttempts=3;
    explicit RegistrationPolicy(bool query_registration=false) : query_registration_(query_registration) {}
    bool ShouldSend(bool connected,bool offer,std::uint64_t now) const {
        if(query_registration_ || !connected || offer || Confirmed() || attempts_>=MaximumAttempts) return false;
        return attempts_==0 || (now>=last_send_ && now-last_send_>=2000000);
    }
    void Sent(std::int64_t id,bool written,std::uint64_t now) {
        if(query_registration_ || attempts_>=MaximumAttempts) return;
        requests_[attempts_++]={id,written};last_send_=now;
    }
    void Identity() { identity_=true; }
    void Ack(std::int64_t id) {
        for(const auto& r:requests_) if(r.written && r.id==id) { ack_=true;return; }
    }
    bool Confirmed() const { return identity_ && (query_registration_ || ack_); }
    unsigned Attempts() const { return attempts_; }
    bool IdentitySeen() const { return identity_; }
    bool AckSeen() const { return ack_; }
private:
    struct Request { std::int64_t id=0; bool written=false; };
    std::array<Request,MaximumAttempts> requests_{};
    unsigned attempts_=0;
    std::uint64_t last_send_=0;
    bool identity_=false,ack_=false;
    bool query_registration_=false;
};
}
