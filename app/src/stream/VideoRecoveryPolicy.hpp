#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace opennow::video {
// A reset requested during a fault must survive healthy queued P pictures and
// be consumed only at an IDR boundary, where references can be rebuilt safely.
inline bool ConsumeResetForIdr(bool idr, std::atomic<bool>& requested, bool scheduled) {
    return idr && (requested.exchange(false) || scheduled);
}
// Caller serializes this state with publication of decoded frames. RTP deltas
// are compared modulo 32 bits; all retained candidates are within one GOP.
class VideoRecoveryPolicy {
public:
    enum class Output { Drop, Present, ConfirmedIdr };
    static std::int32_t Delta(std::uint32_t a, std::uint32_t b) {
        return static_cast<std::int32_t>(a-b);
    }
    bool Required() const { return required_; }
    std::uint64_t Generation() const { return generation_; }
    bool Fault(bool known, std::uint32_t timestamp) {
        if (known && confirmed_ && Delta(timestamp, confirmed_ts_)<=0) return false;
        const bool extend_known=known && required_ && cutoff_known_;
        if (extend_known && Delta(timestamp,cutoff_)>=0 && Delta(timestamp,latest_fault_)<=0) return false;
        if (!known) unbounded_fault_=true;
        ++generation_; required_=true;
        if (extend_known) {
            if(Delta(timestamp,cutoff_)<0) cutoff_=timestamp;
            if(Delta(timestamp,latest_fault_)>0) latest_fault_=timestamp;
        } else { cutoff_=timestamp; latest_fault_=timestamp; }
        cutoff_known_=known && !unbounded_fault_;
        // A delayed report of an older bad picture need not invalidate an
        // already-submitted IDR strictly after every known loss. Its timestamp
        // proves coverage. Unknown failures cannot make that guarantee.
        for(auto& c:candidates_) {
            if(c.used && cutoff_known_ && Delta(c.timestamp,latest_fault_)>0)
                c.generation=generation_;
            else c.used=false;
        }
        return true;
    }
    std::uint32_t UnsafeFrom() const { return cutoff_; }
    bool Queue(std::uint32_t timestamp, bool idr) {
        if(confirmed_ && Delta(timestamp,confirmed_ts_)<0) return false;
        if (idr && EligibleIdr(timestamp)) { Register(timestamp,false); return true; }
        return AllowInput(timestamp,false);
    }
    bool Begin(std::uint32_t timestamp, bool idr) {
        if(confirmed_ && Delta(timestamp,confirmed_ts_)<0) return false;
        if (idr && EligibleIdr(timestamp)) { Register(timestamp,true); return true; }
        return AllowInput(timestamp,true);
    }
    Output Frame(bool known, std::uint32_t timestamp) {
        if (known && confirmed_ && Delta(timestamp,confirmed_ts_)<0) return Output::Drop;
        if (known) for (auto& c:candidates_) {
            if (c.used && c.submitted && c.generation==generation_ && c.timestamp==timestamp) {
                confirmed_=true; confirmed_ts_=timestamp; required_=false;
                cutoff_known_=false; unbounded_fault_=false;
                // Older submitted IDRs must never roll the reference anchor backwards.
                for (auto& old:candidates_)
                    if (old.used && Delta(old.timestamp,timestamp)<=0) old.used=false;
                return Output::ConfirmedIdr;
            }
        }
        if (!required_) return Output::Present;
        return known && cutoff_known_ && Delta(timestamp,cutoff_)<0
            ? Output::Present : Output::Drop;
    }
private:
    struct Candidate {
        std::uint32_t timestamp=0;
        std::uint64_t generation=0;
        bool used=false, submitted=false;
    };
    bool EligibleIdr(std::uint32_t timestamp) const {
        return (!confirmed_ || Delta(timestamp,confirmed_ts_)>=(required_ ? 1 : 0)) &&
            (!required_ || !cutoff_known_ || Delta(timestamp,latest_fault_)>0);
    }
    bool AllowInput(std::uint32_t timestamp, bool require_submitted) const {
        if (!required_) return true;
        if (cutoff_known_ && Delta(timestamp,cutoff_)<0) return true;
        for (const auto& c:candidates_)
            if (c.used && c.generation==generation_ && (!require_submitted || c.submitted) &&
                Delta(timestamp,c.timestamp)>0) return true;
        return false;
    }
    void Register(std::uint32_t timestamp, bool submitted) {
        for (auto& c:candidates_) if(c.used && c.timestamp==timestamp && c.generation==generation_) {
            c.submitted |= submitted; return;
        }
        candidates_[next_]={timestamp,generation_,true,submitted};
        next_=(next_+1)%candidates_.size();
    }
    std::array<Candidate,16> candidates_{};
    unsigned next_=0;
    std::uint64_t generation_=1;
    bool required_=true, confirmed_=false, cutoff_known_=false, unbounded_fault_=false;
    std::uint32_t confirmed_ts_=0, cutoff_=0, latest_fault_=0;
};
}
