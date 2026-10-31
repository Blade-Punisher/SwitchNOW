#pragma once
#include <atomic>
#include <cstdint>
namespace opennow {
// Shared by network completions and UI work; never retain a view to keep it alive.
class GalleryRequestState {
public:
    std::uint64_t Begin() { return ++generation_; }
    bool IsCurrent(std::uint64_t token) const {
        return alive_.load() && generation_.load() == token;
    }
    void Close() { alive_.store(false); ++generation_; }
private:
    std::atomic_bool alive_{true};
    std::atomic_uint64_t generation_{0};
};
}
