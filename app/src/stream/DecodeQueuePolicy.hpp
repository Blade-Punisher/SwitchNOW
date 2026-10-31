#pragma once

#include <algorithm>
#include <cstddef>

namespace opennow::video
{

// Keep roughly 80 ms of complete pictures. The extra frame absorbs short
// retransmission/decode bursts without building a latency-heavy backlog.
constexpr std::size_t MaximumQueuedAccessUnits(int frames_per_second)
{
    const int safe_fps = std::max(1, frames_per_second);
    return static_cast<std::size_t>(
        std::clamp((safe_fps + 11) / 12, 2, 6));
}

constexpr int MaximumDecodeQueueDelayMs()
{
    return 67;
}

// Retransmissions can deliver several complete pictures in one burst. Permit
// three extra units only within one frame interval; never extend the age limit.
constexpr bool DecodeQueueNeedsResync(int fps, std::size_t queued, long long age_us)
{
    if (queued == 0) return false;
    if (age_us >= MaximumDecodeQueueDelayMs() * 1000LL) return true;
    const auto base = MaximumQueuedAccessUnits(fps);
    const bool fresh_burst = age_us < 1000000 / std::max(1, fps);
    return queued >= base + (fresh_burst ? 3 : 0);
}

constexpr std::size_t MaximumDecodeUnitBytes()
{
    return 2 * 1024 * 1024;
}

constexpr std::size_t ReadyFrameQueueCapacity()
{
    return 3;
}

constexpr std::size_t StartupReadyFrameDepth()
{
    return 2;
}

// A ready frame older than this is only discarded when a newer frame is
// already available. Normal FIFO presentation stays untouched; this catches
// the rare post-stall backlog seen in hardware traces.
constexpr int MaximumReadyFrameAgeMs()
{
    // At 60 FPS, keeping a decoded frame for more than about three refreshes
    // only adds visible input lag. Drop it when a newer complete picture is
    // already available; the single-frame path is still never discarded.
    return 50;
}

// Wait only when the ready queue is empty. This adaptive micro-reserve catches
// a decoder completion racing the UI frame without stalling input/render for a
// large part of the refresh interval. Runtime prediction may shorten or skip it.
constexpr int FrameWaitBudgetMicroseconds(int frames_per_second)
{
    const int safe_fps = std::max(1, frames_per_second);
    return std::clamp((1000000 / safe_fps) * 2 / 5, 2500, 6500);
}

} // namespace opennow::video
