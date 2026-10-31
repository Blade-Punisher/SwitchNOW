#pragma once
#include <cstdint>
namespace opennow::streaming {
constexpr bool LiveMaintenanceHandoff(bool peer_completed, bool video_decoded) {
    return peer_completed && video_decoded;
}
constexpr bool MaintenanceDue(std::uint64_t now, std::uint64_t last) {
    return !last || now < last || now - last >= 100000;
}
// Give a queued interaction one more transport pass before routine signaling.
// The deadline is relative to the last completed scheduling decision, so
// continuous input cannot postpone maintenance indefinitely. Startup and
// recovery keep their original deadline.
constexpr bool DeferLiveMaintenance(std::uint64_t now, std::uint64_t last,
                                    bool pending_input, bool urgent) {
    return pending_input && !urgent && last && now >= last &&
        now - last >= 100000 && now - last < 105000;
}
}
