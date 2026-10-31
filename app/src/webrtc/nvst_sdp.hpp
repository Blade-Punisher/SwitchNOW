#pragma once

#include "../stream_settings.hpp"

#include <cstdint>
#include <string>

namespace opennow::webrtc
{

struct RiInputCapabilities
{
    int partial_reliable_threshold_ms = 16;
    std::uint32_t hid_device_mask = 0xffffffffu;
    std::uint32_t partial_reliable_gamepad_mask = 0x0fu;
    std::uint32_t partial_reliable_hid_mask = 0xffffffffu;
};

std::string BuildNvstSdp(
    const std::string& answer_sdp,
    const StreamSettings& settings,
    const RiInputCapabilities& ri_caps);

} // namespace opennow::webrtc
