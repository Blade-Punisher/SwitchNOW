#pragma once
#include "../VideoRecoveryPolicy.hpp"
#include "AVFrameHolder.hpp"
#include <atomic>
#include <functional>
#include <mutex>

namespace opennow::video {
// Owns no pixel buffers. The only publication operation is AVFrame ref/queue
// insertion while the same short lock serializes faults and confirmation.
class DecodedFrameGate {
public:
    struct Event { bool fault; bool known; std::uint32_t timestamp; std::uint64_t generation; };
    void SetObserver(std::function<void(const Event&)> observer) { observer_=std::move(observer); }
    bool Required() const { return required_.load(std::memory_order_acquire); }
    bool Queue(std::uint32_t timestamp, bool idr) {
        if (!idr && !Required()) return true;
        std::lock_guard<std::mutex> lock(mutex_); return policy_.Queue(timestamp,idr);
    }
    bool Begin(std::uint32_t timestamp, bool idr) {
        std::lock_guard<std::mutex> lock(mutex_); return policy_.Begin(timestamp,idr);
    }
    bool Fault(bool known, std::uint32_t timestamp) {
        std::lock_guard<std::mutex> lock(mutex_); return FaultLocked(known,timestamp);
    }
    bool Publish(const AVFrame* frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool known=frame->pts!=AV_NOPTS_VALUE;
        const auto timestamp=static_cast<std::uint32_t>(frame->pts);
        if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags) {
            ++corrupt_; FaultLocked(known,timestamp); return false;
        }
        const auto decision=policy_.Frame(known,timestamp);
        if(decision==VideoRecoveryPolicy::Output::Drop) {
            ++quarantined_; if(!known) ++unknown_pts_; return false;
        }
        AVFrameHolder::instance().push(frame);
        if(decision==VideoRecoveryPolicy::Output::ConfirmedIdr) {
            ++verified_;
            if(observer_) observer_({false,true,timestamp,policy_.Generation()});
        }
        required_.store(policy_.Required(),std::memory_order_release);
        return true;
    }
    std::uint64_t Verified() const { return verified_.load(); }
    std::uint64_t Faults() const { return faults_.load(); }
    std::uint64_t Quarantined() const { return quarantined_.load(); }
    std::uint64_t Corrupt() const { return corrupt_.load(); }
    std::uint64_t UnknownPts() const { return unknown_pts_.load(); }
    std::uint64_t PrunedReady() const { return pruned_.load(); }
private:
    bool FaultLocked(bool known,std::uint32_t timestamp) {
        if(!policy_.Fault(known,timestamp)) return false;
        required_.store(true,std::memory_order_release); ++faults_;
        if(known) pruned_.fetch_add(AVFrameHolder::instance().discardFrom(policy_.UnsafeFrom()));
        if(observer_) observer_({true,known,timestamp,policy_.Generation()});
        return true;
    }
    std::mutex mutex_;
    VideoRecoveryPolicy policy_;
    std::atomic<bool> required_{true};
    std::atomic<std::uint64_t> verified_{0},faults_{0},quarantined_{0},corrupt_{0},unknown_pts_{0},pruned_{0};
    std::function<void(const Event&)> observer_;
};
}
