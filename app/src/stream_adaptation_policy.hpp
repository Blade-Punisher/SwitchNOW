#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>

namespace opennow::streaming {
inline std::string NormalizeMode(const std::string& mode) {
    if (mode == "Performance" || mode == "Original") return "Performance";
    if (mode == "Quality" || mode == "Clarity") return "Quality";
    return "Balanced";
}
enum class Pressure { Stable, DeliveryLoss, SourceSlow, DecoderBusy, DeliveryStall, Recovering };
inline const char* PressureName(Pressure p) {
    switch (p) {
        case Pressure::DeliveryLoss: return "delivery-loss";
        case Pressure::SourceSlow: return "source-or-arrival-slow";
        case Pressure::DecoderBusy: return "decoder-busy";
        case Pressure::DeliveryStall: return "delivery-or-source-pause";
        case Pressure::Recovering: return "recovering";
        default: return "stable";
    }
}
struct Sample {
    float incoming_fps = 0, decoded_fps = 0;
    int target_fps = 60;
    uint64_t damaged_frames = 0;
    size_t decode_backlog = 0;
    uint64_t completed_transport_pauses = 0;
    uint64_t completed_playback_pauses = 0;
    unsigned elapsed_ms = 1000;
    std::uint64_t delivery_jitter_us = 0;
};
enum class PauseCause { None, Transport, VideoSource, VideoAssembly, Uncertain };
inline const char* PauseCauseName(PauseCause cause) {
    switch (cause) {
        case PauseCause::Transport: return "transport";
        case PauseCause::VideoSource: return "video-source";
        case PauseCause::VideoAssembly: return "video-assembly";
        case PauseCause::Uncertain: return "video-delivery-or-source";
        default: return "none";
    }
}
struct PauseEpisode {
    PauseCause cause = PauseCause::None;
    uint64_t duration_us = 0;
    explicit operator bool() const { return cause != PauseCause::None; }
};
// Updated by the network worker. A pause is counted once, on the first new
// access unit, instead of once per 2-second diagnostics window.
struct LivePauseTracker {
    uint64_t begun_us = 0;
    uint64_t last_video_us = 0;
    uint32_t raw_at_start = 0;
    uint32_t last_seen_raw = 0;
    bool raw_progress_during_pause = false;
    bool audio_progress_during_pause = false;
    uint32_t damage_at_start = 0;
    bool damaged_during_pause = false;
    PauseEpisode Observe(uint64_t now_us, uint64_t video_us,
                         uint64_t audio_us, uint32_t raw_video_packets,
                         uint32_t damaged_units = 0) {
        if (video_us == 0 || now_us < video_us) return {};
        if (last_video_us != video_us && begun_us == 0) {
            last_video_us = video_us;
            last_seen_raw = raw_video_packets;
            raw_progress_during_pause = false;
            audio_progress_during_pause = false;
            damage_at_start = damaged_units;
            damaged_during_pause = false;
        }
        if (video_us == last_video_us && raw_video_packets > last_seen_raw)
            raw_progress_during_pause = true;
        last_seen_raw = raw_video_packets;
        damaged_during_pause |= damaged_units != damage_at_start;
        if (begun_us == 0) {
            if (now_us - video_us >= 80000) {
                begun_us = video_us + 16000;
                last_video_us = video_us;
                raw_at_start = raw_video_packets;
                audio_progress_during_pause = audio_us > video_us &&
                    now_us >= audio_us && now_us - audio_us <= 50000;
            }
            return {};
        }
        if (video_us == last_video_us) {
            raw_progress_during_pause |= raw_video_packets > raw_at_start;
            audio_progress_during_pause |= audio_us > begun_us &&
                now_us >= audio_us && now_us - audio_us <= 50000;
            return {};
        }
        // A few raw packets (including retransmissions) do not prove healthy
        // delivery. Audio alone cannot distinguish server stalls from selective
        // video loss. Require actual AU damage to identify assembly recovery.
        const PauseCause cause = damaged_during_pause ? PauseCause::VideoAssembly :
            !raw_progress_during_pause && !audio_progress_during_pause
                ? PauseCause::Transport : PauseCause::Uncertain;
        const PauseEpisode finished {cause, video_us > begun_us ? video_us - begun_us : 0};
        begun_us = 0;
        last_video_us = video_us;
        raw_progress_during_pause = false;
        audio_progress_during_pause = false;
        damage_at_start = damaged_units;
        damaged_during_pause = false;
        return finished;
    }
};
struct Adaptation {
    Pressure pressure = Pressure::Stable;
    unsigned bad_windows = 0, healthy_windows = 0;
    unsigned bad_ms = 0, healthy_ms = 0, recovery_ms = 0;
    unsigned source_slow_ms = 0, source_healthy_ms = 0;
    bool source_slow = false;
    unsigned stable_release_ms = 2000;
    int protection = 0, delivery_guard_us = 0;
    void Update(const Sample& s) {
        const auto elapsed = std::clamp(s.elapsed_ms, 1u, 2000u);
        recovery_ms = recovery_ms > elapsed ? recovery_ms - elapsed : 0;
        // One uneven half-second can be a harmless burst boundary. Confirm
        // moderate starvation, but react immediately to a substantial stall.
        if (s.target_fps > 0 && s.incoming_fps < s.target_fps * .90f) {
            source_slow_ms = std::min(2000u, source_slow_ms + elapsed);
            source_healthy_ms = 0;
            if (source_slow_ms >= 1000 || s.incoming_fps < s.target_fps * .65f)
                source_slow = true;
        } else {
            source_slow_ms = 0;
            if (s.incoming_fps >= s.target_fps * .95f) {
                source_healthy_ms = std::min(2000u, source_healthy_ms + elapsed);
                if (source_healthy_ms >= 1500) source_slow = false;
            } else source_healthy_ms = 0;
        }
        pressure = s.decode_backlog >= 3 && s.decoded_fps < s.incoming_fps * .9f
                ? Pressure::DecoderBusy : s.damaged_frames ? Pressure::DeliveryLoss :
            s.completed_transport_pauses || s.completed_playback_pauses ? Pressure::DeliveryStall :
            source_slow ? Pressure::SourceSlow : Pressure::Stable;
        if (s.completed_transport_pauses || s.completed_playback_pauses) recovery_ms = 1500;
        if (pressure == Pressure::DeliveryLoss) {
            healthy_windows = 0;
            healthy_ms = 0;
            ++bad_windows; bad_ms += elapsed;
            if (bad_ms >= 1000) { protection = std::min(2, protection + 1); bad_ms = 0; bad_windows = 0; }
        } else if (pressure == Pressure::Stable) {
            ++healthy_windows; healthy_ms += elapsed;
            // A single clean half-second must not erase evidence of a burst.
            if (healthy_ms >= 2000) { bad_windows = 0; bad_ms = 0; }
            if (healthy_ms >= 20000) { protection = std::max(0, protection - 1); healthy_windows = 0; healthy_ms = 0; }
        } else { bad_windows = 0; healthy_windows = 0; bad_ms = healthy_ms = 0; }
        if (pressure == Pressure::Stable && recovery_ms) pressure = Pressure::Recovering;
        if (pressure == Pressure::Stable && s.incoming_fps >= s.target_fps * .95f &&
            s.decode_backlog <= 1 && s.delivery_jitter_us >= 4000) {
            const int demand = static_cast<int>(std::min<std::uint64_t>(1500, s.delivery_jitter_us / 4));
            delivery_guard_us = std::min(demand, delivery_guard_us + static_cast<int>(elapsed));
        } else if (pressure != Pressure::Stable) delivery_guard_us = 0;
        else delivery_guard_us = std::max(0, delivery_guard_us - static_cast<int>(elapsed / 2));
        if (pressure == Pressure::SourceSlow || pressure == Pressure::DecoderBusy ||
            pressure == Pressure::DeliveryStall || pressure == Pressure::Recovering)
            stable_release_ms = 0;
        else if (pressure == Pressure::Stable)
            stable_release_ms = std::min(2000u, stable_release_ms + elapsed);
    }
    int WaitBudgetUs(const std::string& mode) const {
        const auto normalized = NormalizeMode(mode);
        if (pressure == Pressure::SourceSlow || pressure == Pressure::DecoderBusy ||
            pressure == Pressure::DeliveryStall || pressure == Pressure::Recovering) return 1000;
        const int base = normalized == "Performance" ? 1500 : normalized == "Quality" ? 4500 : 3000;
        const int normal = std::min(6500, base + protection * 1000 + delivery_guard_us);
        return pressure == Pressure::Stable
            ? 1000 + (normal - 1000) * static_cast<int>(stable_release_ms) / 2000 : normal;
    }
};
inline int RecoveryRetryMs(int rtt_ms) {
    return std::clamp(std::max(0, rtt_ms) * 3 + 50, 200, 600);
}
inline int RecoveryRetryMs(int rtt_ms, unsigned previous_attempts, int media_age_ms) {
    if (previous_attempts == 0) return 200;
    const int base = RecoveryRetryMs(rtt_ms);
    const int multiplier = previous_attempts >= 4 ? 4 : previous_attempts >= 2 ? 2 : 1;
    return std::clamp(base * multiplier + (media_age_ms > 250 ? 250 : 0),
                      200, 1500);
}
inline int RecoveryRetryMs(int rtt_ms, unsigned previous_attempts, int media_age_ms,
                           bool video_progress_since_request) {
    const int normal = RecoveryRetryMs(rtt_ms, previous_attempts, media_age_ms);
    if (previous_attempts == 0 || video_progress_since_request) return normal;
    // An IDR request cannot repair an absent stream. Keep a bounded retry but
    // avoid producing another large IDR every 200 ms during transport silence.
    return std::max(normal, previous_attempts >= 2 ? 1000 : 500);
}
inline int AudioTargetMs(const std::string& mode, int requested_ms) {
    const auto normalized = NormalizeMode(mode);
    const int minimum = normalized == "Quality" ? 50 : normalized == "Performance" ? 30 : 40;
    return std::clamp(std::max(requested_ms, minimum), 30, 100);
}
} // namespace opennow::streaming
