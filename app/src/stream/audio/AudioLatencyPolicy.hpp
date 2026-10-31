#pragma once

#include <algorithm>
#include <cstdint>

namespace opennow::audio
{

constexpr int ClampTargetBufferMs(int target_buffer_ms)
{
    return std::clamp(target_buffer_ms, 30, 100);
}

constexpr int PrimePacketCount(int target_buffer_ms)
{
    return ClampTargetBufferMs(target_buffer_ms) / 10;
}

constexpr int ClampVolumeBoostPercent(int volume_percent)
{
    return std::clamp(volume_percent, 100, 800);
}

constexpr int OutputQueueLimit(int target_buffer_ms)
{
    return std::min(14, PrimePacketCount(target_buffer_ms) + 7);
}

constexpr int MaximumAdaptiveStartBuffers(int target_buffer_ms)
{
    return std::min({10, OutputQueueLimit(target_buffer_ms) - 2,
                    PrimePacketCount(target_buffer_ms) + 5});
}

constexpr std::uint64_t InitialJitterHoldUs(int target_buffer_ms)
{
    return static_cast<std::uint64_t>(ClampTargetBufferMs(target_buffer_ms)) * 1000;
}

constexpr std::uint64_t ResyncJitterHoldUs(int target_buffer_ms)
{
    return static_cast<std::uint64_t>(
        std::max(30, ClampTargetBufferMs(target_buffer_ms) / 2)) * 1000;
}

constexpr std::uint64_t PacketReorderGraceUs()
{
    return 4000;
}

constexpr bool RecentUnderrun(std::uint64_t now_us, std::uint64_t previous_us) {
    return previous_us != 0 && now_us >= previous_us && now_us - previous_us <= 60000000;
}
constexpr bool CanReducePrebuffer(std::uint64_t now_us, std::uint64_t adjusted_us) {
    return now_us >= adjusted_us && now_us - adjusted_us >= 30000000;
}
constexpr int RecoveryStartBuffers(int current, int maximum, unsigned episode_count,
                                   std::uint64_t media_gap_ms, int target_ms) {
    // Protect the next restart from a short burst; a long transport outage
    // cannot be repaired by accumulating extra audio latency.
    return std::min(maximum, current + static_cast<int>(media_gap_ms < 150 &&
        (episode_count >= 2 || media_gap_ms >= static_cast<unsigned>(target_ms / 2))));
}

constexpr std::uint64_t EstimatePlayedSamples(std::uint64_t observed_samples,
                                              std::uint64_t observed_at_us,
                                              std::uint64_t now_us,
                                              std::uint64_t submitted_samples,
                                              std::uint64_t sample_rate)
{
    if (observed_at_us == 0 || now_us <= observed_at_us || sample_rate == 0)
        return std::min(observed_samples, submitted_samples);

    const std::uint64_t elapsed_samples =
        (now_us - observed_at_us) * sample_rate / 1000000;
    return std::min(observed_samples + elapsed_samples, submitted_samples);
}

// audout resets its raw counter when the output is stopped and started again.
// Count the new raw value from zero after that reset so the session clock never
// moves backwards.
constexpr std::uint64_t PlayedSampleDelta(std::uint64_t previous_raw_samples,
                                          std::uint64_t current_raw_samples)
{
    return current_raw_samples >= previous_raw_samples
        ? current_raw_samples - previous_raw_samples
        : current_raw_samples;
}

} // namespace opennow::audio
