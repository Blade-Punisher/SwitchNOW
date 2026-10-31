#pragma once

#include "stream_settings.hpp"
#include "video_quality_policy.hpp"

#include <array>

namespace opennow::settings
{

inline void MarkCustom(StreamSettings& value)
{
    value.preset_id = "custom";
    value.label = "Custom";
}

inline void CycleResolution(StreamSettings& value)
{
    if (value.height >= 1080)
    {
        value.width = 1280;
        value.height = 720;
    }
    else
    {
        value.width = 1920;
        value.height = 1080;
    }
    MarkCustom(value);
}

inline void CycleFrameRate(StreamSettings& value)
{
    value.fps = value.fps >= 60 ? 30 : 60;
    MarkCustom(value);
}

inline void CycleBitrate(StreamSettings& value)
{
    constexpr std::array<int, 6> bitrates {8000, 10000, 12000, 16000, 20000, 25000};
    int next = bitrates.front();
    for (int bitrate : bitrates)
    {
        if (bitrate > value.bitrate_kbps)
        {
            next = bitrate;
            break;
        }
    }
    value.bitrate_kbps = next;
    MarkCustom(value);
}

inline void CycleImageQuality(StreamSettings& value)
{
    value.image_quality_mode = video::NextQualityMode(value.image_quality_mode);
}

} // namespace opennow::settings
