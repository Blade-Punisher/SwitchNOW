#pragma once
#include <cstddef>
#include <cstdint>
namespace opennow::video {
// One already-decoded surplus picture may be retired after a sustained healthy
// 60 Hz interval. This never discards compressed/reference pictures.
class ReadyFrameLatencyPolicy {
public:
    bool Protect(std::uint64_t now) {
        const bool after_trim=last_trim_ && now>=last_trim_ && now-last_trim_<=250000;
        healthy_since_ = 0;
        const auto until=now+(after_trim ? 30000000 : 3000000);
        if(until>quiet_until_) quiet_until_=until;
        if(after_trim) last_trim_=0;
        return after_trim;
    }
    bool Consider(std::uint64_t now, std::int64_t period, std::int64_t jitter,
        std::int64_t producer_interval, std::size_t depth, std::uint64_t oldest_age,
        std::uint64_t next_age, std::int32_t picture_step, bool external_clock) {
        if (now < last_now_) { *this = {}; Protect(now); }
        last_now_ = now;
        if (external_clock || period < 14500 || period > 18000 || jitter < 0 ||
            jitter > period / 3 || producer_interval < period * 3 / 4 ||
            producer_interval > period * 5 / 4) {
            healthy_since_ = 0;
            return false;
        }
        if (now < quiet_until_) return false;
        if (!healthy_since_) healthy_since_ = now;
        if (now - healthy_since_ < 4000000 || depth < 3 ||
            picture_step < 1000 || picture_step > 2000 ||
            oldest_age < static_cast<std::uint64_t>(period * 7 / 4) ||
            next_age > static_cast<std::uint64_t>(period * 5 / 4) ||
            next_age < static_cast<std::uint64_t>(period / 4)) return false;
        last_trim_=now;
        quiet_until_ = now + 15000000; // Prefer smoothness over frequent phase corrections.
        healthy_since_ = 0;
        return true;
    }
private:
    std::uint64_t healthy_since_ = 0, quiet_until_ = 0, last_now_ = 0, last_trim_ = 0;
};
}
