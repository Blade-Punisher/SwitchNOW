#pragma once

#include <string>
#include "stream_adaptation_policy.hpp"

namespace opennow::video
{

struct QualityTuning
{
    int minimum_bitrate_percent;
    int initial_bitrate_percent;
    int fec_repair_min_percent;
    int fec_repair_percent;
    int fec_repair_max_percent;
    int pacing_groups;
    int pacing_min_packets_per_group;
    int pacing_max_delay_us;
    float denoise_strength;
    float sharpen_strength;
    bool preserve_reference_deblocking;
};

inline QualityTuning ResolveQualityTuning(const std::string& mode)
{
    const auto normalized = streaming::NormalizeMode(mode);
    if (normalized == "Performance")
        return {35, 75, 6, 8, 30, 6, 10, 1250, 0.0f, 0.0f, true};
    if (normalized == "Quality")
        return {55, 92, 7, 8, 30, 6, 10, 1250, 0.12f, 0.03f, true};
    return {55, 92, 7, 8, 30, 6, 10, 1250, 0.10f, 0.0f, true};
}

inline std::string NextQualityMode(const std::string& mode)
{
    const auto normalized = streaming::NormalizeMode(mode);
    if (normalized == "Performance") return "Balanced";
    if (normalized == "Balanced") return "Quality";
    return "Performance";
}

} // namespace opennow::video
