#pragma once

#include <cstdint>
#include <algorithm>

namespace opennow::video {

enum class FrameDecision {
    Present,
    HoldPrevious,
    DropSuperseded,
};

struct FrameTimingPolicy {
    // Half of a 60 Hz frame. This tolerates scheduling quantization without
    // presenting a full frame ahead of the audio-derived media clock.
    int32_t maximum_early_ticks = 750;
};

inline int32_t AdaptiveEarlyToleranceTicks(int32_t producer_interval_ticks)
{
    // Keep normal presentation within roughly 10% of the source period.
    // This prevents consuming a future frame early and then repeating the
    // previous picture on the following display tick.
    if (producer_interval_ticks <= 0)
        return 180;
    return producer_interval_ticks < 900
               ? 90
               : producer_interval_ticks > 3000
                     ? 300
                     : producer_interval_ticks / 10;
}

inline int32_t RecoveryEarlyToleranceTicks(int32_t producer_interval_ticks)
{
    // After one repeated display tick, allow a near-future frame through.
    // This bounds repeat streaks without carrying a permanent extra frame.
    if (producer_interval_ticks <= 0)
        return 500;
    const int32_t tolerance = producer_interval_ticks / 3;
    return tolerance < 300 ? 300 : tolerance > 600 ? 600 : tolerance;
}

inline int32_t RtpDelta(uint32_t timestamp, uint32_t target)
{
    return static_cast<int32_t>(timestamp - target);
}

inline bool ShouldResyncForDamagedFrame(uint32_t damaged_timestamp,
                                       uint32_t decoded_idr_timestamp,
                                       bool has_decoded_idr)
{
    return !has_decoded_idr || RtpDelta(damaged_timestamp, decoded_idr_timestamp) > 0;
}

inline int64_t WaitBudgetAfterMisses(int64_t budget_us, unsigned consecutive_timeouts)
{
    return consecutive_timeouts >= 3 && budget_us > 1500 ? 1500 : budget_us;
}

inline int64_t PredictFrameWaitUs(int64_t budget, unsigned misses,
                                  int64_t producer_interval, int64_t jitter,
                                  int64_t since_push) {
    const auto cap = WaitBudgetAfterMisses(budget, misses);
    if (cap <= 0) return 0;
    if (producer_interval <= 0) return cap;
    const auto until = producer_interval - since_push;
    if (until < -cap || until > cap) return 0;
    const auto guard = std::clamp<int64_t>(jitter * 2 + 500, 750, 3000);
    return std::clamp<int64_t>(until + guard, std::min<int64_t>(750, cap), cap);
}

enum class FrameArrivalKind { Regular, TimestampGap, DeliveryGap, Burst };

inline FrameArrivalKind ClassifyFrameArrival(int32_t rtp_delta_ticks,
                                             uint64_t arrival_delta_us,
                                             int32_t expected_ticks)
{
    if (expected_ticks <= 0 || rtp_delta_ticks <= 0) return FrameArrivalKind::Regular;
    if (rtp_delta_ticks > expected_ticks * 3 / 2)
        return FrameArrivalKind::TimestampGap;
    const uint64_t expected_us = static_cast<uint64_t>(expected_ticks) * 1000000 / 90000;
    if (arrival_delta_us > expected_us * 3 / 2)
        return FrameArrivalKind::DeliveryGap;
    if (arrival_delta_us < expected_us / 3)
        return FrameArrivalKind::Burst;
    return FrameArrivalKind::Regular;
}

inline FrameDecision DecideFrame(uint32_t front_timestamp,
                                 bool has_next,
                                 uint32_t next_timestamp,
                                 bool has_previous,
                                 uint32_t target_timestamp,
                                 const FrameTimingPolicy& policy = {})
{
    if (has_previous && RtpDelta(front_timestamp, target_timestamp) > policy.maximum_early_ticks)
        return FrameDecision::HoldPrevious;

    // If another frame is already due, the front frame missed its display
    // opportunity. Presenting it would add latency and create a catch-up burst.
    if (has_next && RtpDelta(next_timestamp, target_timestamp) <= policy.maximum_early_ticks)
        return FrameDecision::DropSuperseded;

    return FrameDecision::Present;
}

} // namespace opennow::video
