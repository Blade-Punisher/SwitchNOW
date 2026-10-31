#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace opennow::video {
// Caller serializes access. Objects are recycled; pixel references are always
// released before caching, so the cache cannot pin decoder/GPU surfaces.
template<class Frame, class Ops, std::size_t Capacity=8> class FrameHeaderPool {
public:
    struct Stats { std::uint64_t allocated=0, reused=0; std::size_t cached=0; };
    ~FrameHeaderPool() { Clear(); }
    FrameHeaderPool() = default;
    FrameHeaderPool(const FrameHeaderPool&) = delete;
    FrameHeaderPool& operator=(const FrameHeaderPool&) = delete;
    Frame* Reference(const Frame* source) {
        if (!source) return nullptr;
        Frame* frame=nullptr;
        if (count_) { frame=cache_[--count_]; ++stats_.reused; }
        else { frame=Ops::Allocate(); if (frame) ++stats_.allocated; }
        if (frame && Ops::Reference(frame,source)<0) Recycle(frame);
        return frame;
    }
    void Recycle(Frame*& frame) {
        if (!frame) return;
        Ops::Unref(frame);
        if (count_<Capacity) cache_[count_++]=std::exchange(frame,nullptr);
        else Ops::Free(&frame);
    }
    Stats GetStats() const { auto result=stats_; result.cached=count_; return result; }
    void Clear() {
        while(count_) Ops::Free(&cache_[--count_]);
        stats_={};
    }
private:
    std::array<Frame*,Capacity> cache_ {};
    std::size_t count_=0;
    Stats stats_ {};
};
}
