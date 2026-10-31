#pragma once
#include "stream_end_policy.hpp"
#include <chrono>
#include <string>

namespace opennow::recovery {
inline bool NeedsOfferClaim(bool offer, bool video, bool /*timed_out*/, unsigned attempt) {
    // The first missing-offer recovery only refreshes endpoints and creates a
    // fresh transport after joining the old workers. Escalate to RESUME once,
    // on the second recovery, instead of claiming the same rig immediately.
    return !offer && !video && attempt >= 2;
}
enum class CloudState { Wait, Ready, Ended };
inline CloudState ClassifyCloudState(int status, bool has_endpoints) {
    // 0 = queue, 1 = provisioning, 6 = confirmation/ads. RESUME can
    // temporarily return any of these while moving the same cloud game.
    if (status == 4 || status == 5) return CloudState::Ended;
    if ((status == 2 || status == 3) && has_endpoints) return CloudState::Ready;
    // Unknown/missing states are not proof of termination. The caller's
    // monotonic deadline bounds them, including malformed ready responses.
    return CloudState::Wait;
}
struct Identity {
    std::string user, provider, provider_url, device, client;
};
inline bool Matches(const Identity& saved, const Identity& expected) {
    return !expected.user.empty() && !expected.provider_url.empty() &&
        !expected.device.empty() && !expected.client.empty() &&
        saved.user == expected.user && saved.provider == expected.provider &&
        saved.provider_url == expected.provider_url && saved.device == expected.device &&
        saved.client == expected.client;
}
inline bool ValidFormat(int width, int height, int fps, const std::string& codec) {
    return width >= 640 && width <= 1920 && height >= 360 && height <= 1080 &&
        (fps == 30 || fps == 60) && codec == "H264";
}
// Retry a stalled first picture only after ICE/DTLS completion. Ordinary
// initial connection errors, explicit close and membership expiry stay terminal.
inline bool Eligible(StreamEndReason reason, bool established) {
    return reason == StreamEndReason::StartupTimedOut ||
        (established && (reason == StreamEndReason::NetworkLost ||
        reason == StreamEndReason::ServerDisconnected ||
        reason == StreamEndReason::VideoTimedOut));
}
inline bool OfferManualStartupRetry(StreamEndReason reason, bool established) {
    return !established && reason == StreamEndReason::ConnectionFailed;
}
class Budget {
public:
    bool Take() { if (attempts_ >= 2) return false; ++attempts_; healthy_since_ = {}; return true; }
    unsigned attempts() const { return attempts_; }
    void Observe(bool fresh, std::chrono::steady_clock::time_point now) {
        if (!fresh) { healthy_since_ = {}; return; }
        if (healthy_since_ == std::chrono::steady_clock::time_point{}) healthy_since_ = now;
        if (now - healthy_since_ >= std::chrono::seconds(30)) attempts_ = 0;
    }
    void ManualRetry() { attempts_ = 0; healthy_since_ = {}; }
private:
    unsigned attempts_ = 0;
    std::chrono::steady_clock::time_point healthy_since_ {};
};
} // namespace opennow::recovery
