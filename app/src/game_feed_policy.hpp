#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
namespace opennow::game_feed {
inline std::atomic<std::uint64_t> revision{0};
inline std::uint64_t Revision(){return revision.load();}
class Deferred : public std::runtime_error {
public: Deferred() : std::runtime_error("Game feed refresh deferred for game launch") {}
};
// Latch cancellation reason: a completed launch must not turn cancellation into an error dialog.
class LaunchGate {
    bool deferred_ = false;
public:
    bool Check(bool launching) { deferred_ = deferred_ || launching; return deferred_; }
    bool deferred() const { return deferred_; }
};
class Requests {
    std::atomic<std::uint64_t> generation_{0};
public:
    std::uint64_t Begin() { return ++generation_; }
    void Invalidate() { ++generation_; }
    bool Current(std::uint64_t ticket) const { return generation_.load() == ticket; }
};
inline std::string ScopeKey(const std::string& feed, const std::string& owner) {
    return owner.empty() ? std::string{} : std::to_string(feed.size()) + ":" + feed + owner;
}
inline bool Fresh(std::int64_t timestamp, std::int64_t now, std::int64_t maximum_age) {
    return timestamp > 0 && now >= timestamp && now - timestamp < maximum_age;
}
inline constexpr std::int64_t kPanelAgeMs = 2 * 60 * 1000;
inline constexpr std::int64_t kFullScanAgeMs = 6 * 60 * 60 * 1000;
inline std::string OwnerKey(const std::string& user, const std::string& provider, const std::string& service) {
    if (user.empty()) return {};
    return std::to_string(user.size()) + ":" + user +
        std::to_string(provider.size()) + ":" + provider +
        std::to_string(service.size()) + ":" + service;
}
inline bool ScanRequired(bool force, bool has_cache, std::int64_t scanned_at, std::int64_t now) {
    return force || !has_cache || !Fresh(scanned_at, now, kFullScanAgeMs);
}
inline bool ShowFailure(bool deferred, bool automatic, bool frontmost, bool same_account) {
    return !deferred && !automatic && frontmost && same_account;
}
}
