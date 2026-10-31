#include "AudioPipeline.hpp"
#include "AudioLatencyPolicy.hpp"
#include "AudioRtpUtils.hpp"
#include "AudioServiceDeadline.hpp"
#include "AudioRecoveryPolicy.hpp"
#include "AudioGainPolicy.hpp"
#include "../../stream_diagnostics.hpp"

#include "peer_connection.h"
#include <switch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <deque>
#include <malloc.h>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = 480; // NVIDIA advertises ptime=10 ms.
constexpr int kPrimePackets = 4;
constexpr int kMaxPackets = 12;
constexpr int kOutputBufferCount = 24;
constexpr size_t kOutputBufferBytes = 0x4000;
constexpr int kOutputStartBuffers = 4;
constexpr int kFadeSilenceFrames = 480;  // 10 ms
constexpr int kFadeTotalFrames = 4800;   // 100 ms
constexpr int kConcealmentRecoveryFrames = 240; // 5 ms

uint64_t monotonic_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void write_be32(uint8_t* p, uint32_t value) {
    p[0] = uint8_t(value >> 24);
    p[1] = uint8_t(value >> 16);
    p[2] = uint8_t(value >> 8);
    p[3] = uint8_t(value);
}

}

struct AudioPipeline::Impl {
    struct Packet {
        std::vector<uint8_t> payload;
        uint32_t timestamp = 0;
        uint32_t ssrc = 0;
        uint16_t sequence = 0;
        uint8_t payload_type = 0;
        uint64_t arrival_us = 0;
        uint64_t epoch = 0;
    };

    std::atomic<bool> running {false};
    std::atomic<bool> reset_epoch_requested {false};
    std::atomic<uint64_t> media_epoch {0};
    std::atomic<uint64_t> last_receive_us {0};
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<Packet> packets;
    uint16_t expected_sequence = 0;
    bool have_expected = false;
    bool primed = false;
    int volume_percent = 600;
    float applied_gain = 1.0f;
    opennow::audio::GainEnvelope gain_envelope;
    opennow::audio::ArrivalJitter arrival_jitter;
    std::atomic<uint64_t> arrival_jitter_us {0}, start_target_ms {40};
    std::atomic<int> last_input_peak {0};
    std::atomic<int> last_input_rms {0};
    std::atomic<int> last_output_peak {0};
    std::atomic<int> last_output_rms {0};
    std::atomic<int> last_applied_gain_x100 {100};
    std::atomic<uint64_t> limited_frames {0};
    int prime_packets = kPrimePackets;
    int output_start_buffers = kOutputStartBuffers;
    int maximum_output_start_buffers = kOutputStartBuffers + 2;
    int output_queue_limit = 8;
    opennow::audio::ReserveRelease reserve_release;
    opennow::audio::ShortGapReserve short_gap_reserve;
    opennow::audio::EmptyQueueGrace empty_queue_grace;
    uint64_t rebuffer_us_total = 0, rebuffer_us_max = 0;
    uint64_t last_underrun_us = 0;
    uint64_t underrun_episode_start_us = 0;
    uint64_t underrun_restart_us = 0;
    unsigned underruns_in_episode = 0;
    uint64_t initial_jitter_hold_us = 40000;
    uint64_t resync_jitter_hold_us = 30000;
    uint64_t jitter_hold_until_us = 0;
    uint64_t missing_packet_since_us = 0;
    int fade_frames_processed = 0;
    int concealment_recovery_frames_remaining = 0;
    std::array<int16_t, kChannels> last_output_samples {};

    HwopusDecoder decoder {};
    bool decoder_ready = false;
    struct OutputSlot {
        AudioOutBuffer buffer {};
        uint8_t* storage = nullptr;
        bool free = true;
    };
    std::array<OutputSlot, kOutputBufferCount> output_slots {};
    std::atomic<bool> output_ready {false};
    std::atomic<bool> output_started {false};
    size_t queued_output_buffers = 0;
    uint64_t last_raw_played_samples = 0;
    uint64_t accumulated_played_samples = 0;
    bool have_raw_played_samples = false;
    std::atomic<uint64_t> submitted_samples {0};
    std::atomic<uint64_t> played_samples {0};
    std::atomic<uint64_t> played_samples_observed_at_us {0};
    std::atomic<uint64_t> base_timestamp {0};
    std::atomic<bool> have_base_timestamp {false};
    std::atomic<uint32_t> audio_ssrc {0};
    mutable std::mutex sender_report_mutex;
    uint64_t sr_ntp_us = 0;
    uint32_t sr_rtp_timestamp = 0;
    std::atomic<bool> have_sender_report {false};

    std::atomic<uint64_t> packets_rx {0};
    std::atomic<uint64_t> packets_decoded {0};
    std::atomic<uint64_t> decode_attempts {0};
    std::atomic<uint64_t> samples_decoded {0};
    std::atomic<uint64_t> sequence_gaps {0};
    std::atomic<uint64_t> late_drops {0};
    std::atomic<uint64_t> queue_drops {0};
    std::atomic<uint64_t> output_throttle_drops {0};
    std::atomic<uint64_t> decode_errors {0};
    std::atomic<uint64_t> plc_frames {0};
    std::atomic<uint64_t> underruns {0};
    std::atomic<uint64_t> played_counter_resets {0};
    std::atomic<uint64_t> output_queue_high_water {0};
    std::atomic<uint64_t> red_packets {0};
    std::atomic<uint64_t> red_recoveries {0};
    std::atomic<uint64_t> epoch_resets {0};
    uint64_t session_started_us = 0;
    uint64_t last_wait_log_us = 0;

    void log(const std::string& message) {
        if (!opennow::StreamDiagnosticsEnabled())
            return;
        opennow::AppendAudioDiagnostic(std::to_string(monotonic_us()) + " " + message + "\n");
    }

    bool initialize_audio() {
        Result rc = hwopusDecoderInitialize(&decoder, kSampleRate, kChannels);
        if (R_FAILED(rc)) {
            log("INIT hwopus_failed rc=0x" + hex_result(rc));
            return false;
        }
        decoder_ready = true;

        rc = audoutInitialize();
        if (R_FAILED(rc)) {
            log("INIT audout_failed rc=0x" + hex_result(rc));
            return false;
        }
        if (audoutGetSampleRate() != kSampleRate || audoutGetChannelCount() != kChannels ||
            audoutGetPcmFormat() != PcmFormat_Int16) {
            log("INIT audout_format_unsupported rate=" + std::to_string(audoutGetSampleRate()) +
                " channels=" + std::to_string(audoutGetChannelCount()) +
                " format=" + std::to_string(static_cast<int>(audoutGetPcmFormat())));
            audoutExit();
            return false;
        }
        for (auto& slot : output_slots) {
            slot.storage = static_cast<uint8_t*>(memalign(0x1000, kOutputBufferBytes));
            if (!slot.storage) {
                log("INIT audout_buffer_allocation_failed");
                for (auto& allocated : output_slots) {
                    free(allocated.storage);
                    allocated.storage = nullptr;
                }
                audoutExit();
                return false;
            }
            std::memset(slot.storage, 0, kOutputBufferBytes);
            slot.buffer.next = nullptr;
            slot.buffer.buffer = slot.storage;
            slot.buffer.buffer_size = kOutputBufferBytes;
            slot.buffer.data_size = 0;
            slot.buffer.data_offset = 0;
            slot.free = true;
        }
        u64 raw_played = 0;
        if (R_SUCCEEDED(audoutGetAudioOutPlayedSampleCount(&raw_played))) {
            last_raw_played_samples = raw_played;
            accumulated_played_samples = 0;
            have_raw_played_samples = true;
        }
        output_ready.store(true);
        log("INIT ok backend=audout rate=48000 channels=2 frameSamples=480 prebuffer=" +
            std::to_string(prime_packets) + " buffers=24 outputLimit=" +
            std::to_string(output_queue_limit) + " startupHoldMs=" +
            std::to_string(initial_jitter_hold_us / 1000) + " gain=" +
            std::to_string(volume_percent));
        return true;
    }

    static std::string hex_result(Result rc) {
        char text[16];
        std::snprintf(text, sizeof(text), "%08X", static_cast<unsigned int>(rc));
        return text;
    }

    void shutdown_audio() {
        if (output_ready.exchange(false)) {
            if (output_started) {
                bool flushed = false;
                (void)audoutFlushAudioOutBuffers(&flushed);
                (void)audoutStopAudioOut();
                output_started = false;
            }
            for (auto& slot : output_slots) {
                free(slot.storage);
                slot.storage = nullptr;
                slot.free = true;
            }
            queued_output_buffers = 0;
            audoutExit();
        }
        if (decoder_ready) {
            hwopusDecoderExit(&decoder);
            decoder_ready = false;
        }
    }

    void refresh_played_samples() {
        u64 raw_played = 0;
        if (R_FAILED(audoutGetAudioOutPlayedSampleCount(&raw_played)))
            return;
        if (!have_raw_played_samples) {
            last_raw_played_samples = raw_played;
            have_raw_played_samples = true;
        } else {
            const bool counter_reset = raw_played < last_raw_played_samples;
            accumulated_played_samples += opennow::audio::PlayedSampleDelta(
                last_raw_played_samples, raw_played);
            last_raw_played_samples = raw_played;
            if (counter_reset) {
                const uint64_t resets = played_counter_resets.fetch_add(1) + 1;
                log("AUDOUT played_counter_reset raw=" + std::to_string(raw_played) +
                    " accumulated=" + std::to_string(accumulated_played_samples) +
                    " count=" + std::to_string(resets));
            }
        }
        played_samples.store(
            std::min(accumulated_played_samples, submitted_samples.load()),
            std::memory_order_relaxed);
        played_samples_observed_at_us.store(monotonic_us(), std::memory_order_release);
    }

    void reclaim_output_buffers() {
        AudioOutBuffer* released = nullptr;
        u32 released_count = 0;
        while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &released_count)) &&
               released && released_count > 0) {
            for (auto& slot : output_slots) {
                if (&slot.buffer == released) {
                    if (!slot.free && queued_output_buffers > 0)
                        --queued_output_buffers;
                    slot.free = true;
                    break;
                }
            }
            released = nullptr;
            released_count = 0;
        }
        refresh_played_samples();
        const auto received_us = last_receive_us.load(std::memory_order_acquire);
        const auto checked_us = monotonic_us();
        if (!received_us || checked_us < received_us || checked_us - received_us >= 150000)
            reserve_release = {}; // silence is not evidence of stable low jitter
        if (output_started && queued_output_buffers > 0 && empty_queue_grace.began_us) {
            AudioOutState state = AudioOutState_Started;
            if (R_SUCCEEDED(audoutGetAudioOutState(&state)) && state == AudioOutState_Stopped) {
                if (R_FAILED(audoutStartAudioOut())) {
                    output_started = false;
                    underrun_restart_us = checked_us;
                    log("AUDOUT soft_resume_start_failed; using normal priming");
                }
            }
            if (output_started) empty_queue_grace.Filled(checked_us);
            else empty_queue_grace.Reprimed();
        }
        if (output_started && queued_output_buffers == 0) {
            const auto media_gap = received_us && checked_us >= received_us
                ? (checked_us - received_us) / 1000 : 150;
            const auto grace_us = opennow::audio::EmptyGraceBudgetUs(
                arrival_jitter_us.load(std::memory_order_relaxed), media_gap, underruns_in_episode);
            if (!empty_queue_grace.MustReprime(checked_us, grace_us)) return;
            empty_queue_grace.Reprimed();
            (void)audoutStopAudioOut();
            output_started = false;
            const uint64_t now_us = monotonic_us();
            const uint64_t media_us = last_receive_us.load(std::memory_order_acquire);
            const uint64_t media_gap_ms = media_us && now_us >= media_us
                ? (now_us - media_us) / 1000 : 0;
            if (!opennow::audio::RecentUnderrun(now_us, last_underrun_us)) {
                underrun_episode_start_us = now_us;
                underruns_in_episode = 0;
            }
            ++underruns_in_episode;
            last_underrun_us = now_us;
            underrun_restart_us = now_us;
            const uint64_t count = underruns.fetch_add(1) + 1;
            short_gap_reserve.Observe(now_us, media_gap_ms, prime_packets);
            output_start_buffers = opennow::audio::RecoveryReserve(
                output_start_buffers, prime_packets, maximum_output_start_buffers, underruns_in_episode,
                arrival_jitter_us.load(std::memory_order_relaxed), media_gap_ms);
            const int demand = short_gap_reserve.Demand(now_us, prime_packets, maximum_output_start_buffers,
                opennow::audio::RecoveryReserve(prime_packets, prime_packets,
                    maximum_output_start_buffers, underruns_in_episode,
                    arrival_jitter_us.load(std::memory_order_relaxed), media_gap_ms));
            if (media_gap_ms < 150) output_start_buffers = std::max(output_start_buffers, demand);
            if (media_gap_ms < 150)
                output_start_buffers = reserve_release.Observe(now_us, output_start_buffers, demand);
            else reserve_release = {};
            start_target_ms.store(output_start_buffers * 10, std::memory_order_relaxed);
            log("AUDOUT underrun; re-priming output count=" + std::to_string(count) +
                " emptyGraceUs=" + std::to_string(grace_us) +
                " nextPrebuffer=" + std::to_string(output_start_buffers) +
                " mediaGapMs=" + std::to_string(media_gap_ms) +
                " arrivalJitterUs=" + std::to_string(arrival_jitter_us.load()) +
                " episodeCount=" + std::to_string(underruns_in_episode) +
                " shortGapReserve=" + std::to_string(short_gap_reserve.Demand(now_us, prime_packets,
                    maximum_output_start_buffers, prime_packets) - prime_packets));
        } else if (output_started && output_start_buffers > prime_packets) {
            // Lower only the next priming target: never discard live PCM or
            // jump the playback clock just to reduce the protection reserve.
            const int demand = short_gap_reserve.Demand(monotonic_us(), prime_packets, maximum_output_start_buffers,
                opennow::audio::RecoveryReserve(prime_packets, prime_packets,
                    maximum_output_start_buffers, underruns_in_episode, arrival_jitter_us.load(), 0));
            const int reduced = reserve_release.Observe(monotonic_us(), output_start_buffers, demand);
            if (reduced != output_start_buffers) {
                output_start_buffers = reduced;
                start_target_ms.store(output_start_buffers * 10, std::memory_order_relaxed);
                log("AUDOUT stable_jitter nextPrebuffer=" + std::to_string(output_start_buffers));
            }
        }
    }

    OutputSlot* acquire_output_slot() {
        reclaim_output_buffers();
        for (auto& slot : output_slots)
            if (slot.free)
                return &slot;
        return nullptr;
    }

    bool ensure_output_started() {
        if (output_started || !opennow::audio::CanRestartOutput(
                queued_output_buffers, output_start_buffers, prime_packets,
                monotonic_us(), underrun_restart_us))
            return true;
        const Result rc = audoutStartAudioOut();
        if (R_FAILED(rc)) {
            log("AUDOUT start_failed rc=0x" + hex_result(rc));
            return false;
        }
        output_started = true;
        if (underrun_restart_us && monotonic_us() >= underrun_restart_us) {
            const auto duration = monotonic_us() - underrun_restart_us;
            rebuffer_us_total += duration; rebuffer_us_max = std::max(rebuffer_us_max, duration);
        }
        log("AUDOUT started queued=" + std::to_string(queued_output_buffers) +
            " recoveryMs=" + std::to_string(underrun_restart_us &&
                monotonic_us() >= underrun_restart_us
                    ? (monotonic_us() - underrun_restart_us) / 1000 : 0));
        underrun_restart_us = 0;
        return true;
    }

    void submit_pcm(const int16_t* pcm, int samples) {
        if (!output_ready.load() || !pcm || samples <= 0)
            return;
        reclaim_output_buffers();
        if (queued_output_buffers >= static_cast<size_t>(output_queue_limit)) {
            const uint64_t drops = output_throttle_drops.fetch_add(1) + 1;
            queue_drops++;
            if (drops == 1 || drops % 50 == 0)
                log("AUDOUT live_edge_drop queued=" + std::to_string(queued_output_buffers) +
                    " limit=" + std::to_string(output_queue_limit) +
                    " count=" + std::to_string(drops));
            return;
        }
        const auto* source = reinterpret_cast<const uint8_t*>(pcm);
        size_t remaining = static_cast<size_t>(samples) * kChannels * sizeof(int16_t);
        while (remaining > 0 && running.load()) {
            OutputSlot* slot = acquire_output_slot();
            if (!slot) {
                queue_drops++;
                log("AUDOUT no_free_buffer queued=" + std::to_string(queued_output_buffers));
                return;
            }
            const size_t copy_size = std::min(remaining, kOutputBufferBytes);
            std::memcpy(slot->storage, source, copy_size);
            armDCacheFlush(slot->storage, copy_size);
            slot->buffer.next = nullptr;
            slot->buffer.data_offset = 0;
            slot->buffer.data_size = copy_size;
            if (decode_attempts.load() <= 12)
                log("AUDOUT enqueue_begin bytes=" + std::to_string(copy_size));
            Result rc = audoutAppendAudioOutBuffer(&slot->buffer);
            if (R_FAILED(rc) && !output_started) {
                const Result start_rc = audoutStartAudioOut();
                if (R_SUCCEEDED(start_rc)) {
                    output_started = true;
                    rc = audoutAppendAudioOutBuffer(&slot->buffer);
                }
            }
            if (R_FAILED(rc)) {
                queue_drops++;
                log("AUDOUT enqueue_failed rc=0x" + hex_result(rc));
                bool contains = false;
                slot->free = R_FAILED(audoutContainsAudioOutBuffer(&slot->buffer, &contains)) || !contains;
                return;
            }
            slot->free = false;
            ++queued_output_buffers;
            uint64_t observed_high_water =
                output_queue_high_water.load(std::memory_order_relaxed);
            while (observed_high_water < queued_output_buffers &&
                   !output_queue_high_water.compare_exchange_weak(
                       observed_high_water, queued_output_buffers,
                       std::memory_order_relaxed)) {}
            const uint64_t frame_count = copy_size / (kChannels * sizeof(int16_t));
            submitted_samples.fetch_add(frame_count);
            if (decode_attempts.load() <= 12)
                log("AUDOUT enqueue_ok frames=" + std::to_string(frame_count) +
                    " queued=" + std::to_string(queued_output_buffers));
            if (!ensure_output_started())
                return;
            source += copy_size;
            remaining -= copy_size;
        }
        refresh_played_samples();
    }

    void submit_plc() {
        std::array<int16_t, kFrameSamples * kChannels> concealment {};
        for (int frame = 0; frame < kFrameSamples; ++frame) {
            const float envelope = static_cast<float>(kFrameSamples - frame - 1) /
                                   static_cast<float>(kFrameSamples);
            for (int channel = 0; channel < kChannels; ++channel) {
                concealment[frame * kChannels + channel] = static_cast<int16_t>(
                    std::lround(static_cast<float>(last_output_samples[channel]) * envelope));
            }
        }
        last_output_samples.fill(0);
        concealment_recovery_frames_remaining = kConcealmentRecoveryFrames;
        submit_pcm(concealment.data(), kFrameSamples);
        plc_frames++;
    }

    void apply_volume_boost(int16_t* pcm, int sample_count, uint64_t attempt) {
        if (!pcm || sample_count <= 0)
            return;

        int input_peak = 0;
        uint64_t input_square_sum = 0;
        for (int i = 0; i < sample_count; ++i) {
            const int sample = static_cast<int>(pcm[i]);
            input_peak = std::max(input_peak, std::abs(sample));
            input_square_sum += static_cast<uint64_t>(static_cast<int64_t>(sample) * sample);
        }

        const float requested_gain = static_cast<float>(volume_percent) / 100.0f;
        const float gain_at_block_start = applied_gain;
        applied_gain = gain_envelope.Update(input_peak, requested_gain,
            static_cast<unsigned>(sample_count / kChannels));
        if (applied_gain + 0.01f < requested_gain)
            limited_frames++;

        int output_peak = 0;
        uint64_t output_square_sum = 0;
        for (int i = 0; i < sample_count; ++i) {
            const int frame_in_block = i / kChannels;
            const int frames_in_block = std::max(1, sample_count / kChannels);
            const float gain_progress = static_cast<float>(frame_in_block + 1) /
                                        static_cast<float>(frames_in_block);
            const float sample_gain = applied_gain < gain_at_block_start
                ? applied_gain
                : gain_at_block_start +
                      (applied_gain - gain_at_block_start) * gain_progress;
            float fade = 1.0f;
            if (fade_frames_processed < kFadeTotalFrames) {
                const int frame = fade_frames_processed + i / kChannels;
                if (frame < kFadeSilenceFrames) {
                    fade = 0.0f;
                } else {
                    fade = std::min(1.0f, static_cast<float>(frame - kFadeSilenceFrames) /
                                              static_cast<float>(kFadeTotalFrames - kFadeSilenceFrames));
                }
            }
            if (concealment_recovery_frames_remaining > 0) {
                const int recovery_progress =
                    kConcealmentRecoveryFrames -
                    concealment_recovery_frames_remaining + frame_in_block;
                fade *= std::clamp(
                    static_cast<float>(recovery_progress + 1) /
                        static_cast<float>(kConcealmentRecoveryFrames),
                    0.0f, 1.0f);
            }
            int32_t amplified = static_cast<int32_t>(
                std::lround(static_cast<float>(pcm[i]) * sample_gain * fade));
            amplified = std::max<int32_t>(-32768, std::min<int32_t>(32767, amplified));
            pcm[i] = static_cast<int16_t>(amplified);
            output_peak = std::max(output_peak, std::abs(static_cast<int>(pcm[i])));
            output_square_sum += static_cast<uint64_t>(static_cast<int64_t>(pcm[i]) * pcm[i]);
        }
        fade_frames_processed = std::min(kFadeTotalFrames,
                                         fade_frames_processed + sample_count / kChannels);
        concealment_recovery_frames_remaining = std::max(
            0, concealment_recovery_frames_remaining - sample_count / kChannels);
        if (sample_count >= kChannels) {
            for (int channel = 0; channel < kChannels; ++channel)
                last_output_samples[channel] = pcm[sample_count - kChannels + channel];
        }

        const int input_rms = static_cast<int>(std::sqrt(
            static_cast<double>(input_square_sum) / static_cast<double>(sample_count)));
        const int output_rms = static_cast<int>(std::sqrt(
            static_cast<double>(output_square_sum) / static_cast<double>(sample_count)));
        last_input_peak.store(input_peak);
        last_input_rms.store(input_rms);
        last_output_peak.store(output_peak);
        last_output_rms.store(output_rms);
        last_applied_gain_x100.store(static_cast<int>(std::lround(applied_gain * 100.0f)));

        if (attempt == 1 || attempt % 500 == 0) {
            log("LEVEL inputPeak=" + std::to_string(input_peak) +
                " inputRms=" + std::to_string(input_rms) +
                " outputPeak=" + std::to_string(output_peak) +
                " outputRms=" + std::to_string(output_rms) +
                " requestedGainX100=" + std::to_string(volume_percent) +
                " appliedGainX100=" + std::to_string(last_applied_gain_x100.load()) +
                " limitedFrames=" + std::to_string(limited_frames.load()));
        }
    }

    void reset_media_epoch(const char* reason) {
        if (output_ready.load()) {
            // Do not re-arm buffers still owned by audout. They are reclaimed
            // normally, keeping the restart click-free and avoiding 0x1099.
            reclaim_output_buffers();
        }
        if (decoder_ready) {
            hwopusDecoderExit(&decoder);
            decoder_ready = R_SUCCEEDED(hwopusDecoderInitialize(&decoder, kSampleRate, kChannels));
        }
        have_base_timestamp.store(false);
        fade_frames_processed = 0;
        concealment_recovery_frames_remaining = 0;
        last_output_samples.fill(0);
        epoch_resets++;
        log("EPOCH reset reason=" + std::string(reason ? reason : "transport_gap") +
            " count=" + std::to_string(epoch_resets.load()) +
            " audoutQueued=" + std::to_string(queued_output_buffers));
    }

    void decode_packet(const Packet& packet) {
        const uint64_t attempt = decode_attempts.fetch_add(1) + 1;
        if (attempt <= 12)
            log("DECODE begin attempt=" + std::to_string(attempt) +
                " seq=" + std::to_string(packet.sequence) +
                " pt=" + std::to_string(packet.payload_type) +
                " bytes=" + std::to_string(packet.payload.size()) +
                " rtp=" + std::to_string(packet.timestamp));
        if (!decoder_ready) {
            decode_errors++;
            submit_plc();
            return;
        }
        opennow::audio::ParsedPayload payload =
            opennow::audio::ParseRedPrimary(packet.payload.data(), packet.payload.size(), packet.payload_type);
        if (!payload.data || payload.size == 0) {
            decode_errors++;
            log("DECODE malformed_red seq=" + std::to_string(packet.sequence));
            submit_plc();
            return;
        }
        if (payload.red)
            red_packets++;

        std::vector<uint8_t> input(sizeof(HwopusHeader) + payload.size);
        write_be32(input.data(), static_cast<uint32_t>(payload.size));
        write_be32(input.data() + 4, 0);
        std::memcpy(input.data() + sizeof(HwopusHeader), payload.data, payload.size);
        std::array<int16_t, 5760 * kChannels> pcm {};
        int32_t decoded_bytes = 0;
        int32_t decoded_samples = 0;
        const Result rc = hwopusDecodeInterleaved(&decoder, &decoded_bytes, &decoded_samples,
                                                   input.data(), input.size(), pcm.data(),
                                                   pcm.size() * sizeof(int16_t));
        if (attempt <= 12)
            log("DECODE hwopus_return attempt=" + std::to_string(attempt) +
                " rc=0x" + hex_result(rc) +
                " samples=" + std::to_string(decoded_samples) +
                " consumed=" + std::to_string(decoded_bytes));
        if (R_FAILED(rc) || decoded_samples <= 0) {
            decode_errors++;
            log("DECODE failed seq=" + std::to_string(packet.sequence) +
                " pt=" + std::to_string(packet.payload_type) +
                " bytes=" + std::to_string(payload.size) + " rc=0x" + hex_result(rc));
            submit_plc();
            return;
        }
        if (!have_base_timestamp) {
            // Account for startup concealment already queued before the first real packet.
            base_timestamp.store(opennow::audio::AudioTimelineBase(packet.timestamp, submitted_samples.load()));
            have_base_timestamp.store(true);
            log("CLOCK base rtp=" + std::to_string(base_timestamp.load()) +
                " packetRtp=" + std::to_string(packet.timestamp) +
                " preRollSamples=" + std::to_string(submitted_samples.load()) +
                " seq=" + std::to_string(packet.sequence));
        }
        audio_ssrc.store(packet.ssrc);
        apply_volume_boost(pcm.data(), decoded_samples * kChannels, attempt);
        submit_pcm(pcm.data(), decoded_samples);
        if (attempt <= 12)
            log("DECODE submit_done attempt=" + std::to_string(attempt) +
                " submitted=" + std::to_string(submitted_samples.load()));
        packets_decoded++;
        samples_decoded += decoded_samples;
        if (packets_decoded.load() == 1 || packets_decoded.load() % 500 == 0) {
            size_t queue_size = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                queue_size = packets.size();
            }
            log("PROGRESS packets=" + std::to_string(packets_decoded.load()) +
                " samples=" + std::to_string(samples_decoded.load()) +
                " queue=" + std::to_string(queue_size) +
                " played=" + std::to_string(played_samples.load()));
        }
    }

    bool take_next(Packet& packet) {
        std::unique_lock<std::mutex> lock(mutex);
        const opennow::audio::ServiceDeadline deadline(monotonic_us());
        for (;;) {
            // Return to the output service even when RTP has stopped. Reset
            // must be consumed before extracting a packet of the new epoch.
            const uint64_t now_us = monotonic_us();
            if (deadline.ShouldService(now_us, running.load(), reset_epoch_requested.load()))
                return false;
            const bool hold_finished =
                jitter_hold_until_us == 0 || now_us >= jitter_hold_until_us;
            if (packets.empty() || !hold_finished ||
                (!primed && packets.size() < static_cast<size_t>(prime_packets))) {
                uint64_t wait_us = deadline.WaitUs(now_us, 12000);
                if (!hold_finished)
                    wait_us = std::min(wait_us, jitter_hold_until_us - now_us);
                cv.wait_for(lock, std::chrono::microseconds(wait_us));
                continue;
            }

            if (!primed) {
                primed = true;
                expected_sequence = packets.front().sequence;
                have_expected = true;
                jitter_hold_until_us = 0;
                missing_packet_since_us = 0;
                log("JITTER primed packets=" + std::to_string(packets.size()) +
                    " seq=" + std::to_string(expected_sequence));
            }

            while (!packets.empty() &&
                   static_cast<int16_t>(packets.front().sequence - expected_sequence) < 0) {
                packets.pop_front();
                late_drops++;
            }
            if (packets.empty()) {
                missing_packet_since_us = 0;
                continue;
            }

            auto found = std::find_if(packets.begin(), packets.end(), [this](const Packet& item) {
                return item.sequence == expected_sequence;
            });
            if (found != packets.end()) {
                packet = std::move(*found);
                packets.erase(found);
                expected_sequence++;
                missing_packet_since_us = 0;
                return true;
            }

            auto next = std::find_if(packets.begin(), packets.end(), [this](const Packet& item) {
                return item.sequence == static_cast<uint16_t>(expected_sequence + 1) &&
                       item.payload_type == 63;
            });
            if (next != packets.end()) {
                const auto redundant = opennow::audio::ParseFirstRedundant(
                    next->payload.data(), next->payload.size());
                if (redundant.data && redundant.size > 0) {
                    packet.payload.assign(redundant.data, redundant.data + redundant.size);
                    packet.timestamp = next->timestamp - redundant.timestamp_offset;
                    packet.ssrc = next->ssrc;
                    packet.sequence = expected_sequence;
                    packet.payload_type = 111;
                    packet.arrival_us = next->arrival_us;
                    packet.epoch = next->epoch;
                    expected_sequence++;
                    missing_packet_since_us = 0;
                    red_recoveries++;
                    return true;
                }
            }

            if (missing_packet_since_us == 0)
                missing_packet_since_us = now_us;
            const uint64_t missing_us = now_us - missing_packet_since_us;
            const uint64_t grace_us = opennow::audio::PacketReorderGraceUs();
            if (missing_us < grace_us) {
                cv.wait_for(lock, std::chrono::microseconds(deadline.WaitUs(
                    now_us, grace_us - missing_us)));
                continue;
            }

            sequence_gaps++;
            expected_sequence++;
            missing_packet_since_us = 0;
            packet.epoch = media_epoch.load();
            return true; // Empty payload means PLC.
        }
    }

    void run() {
        session_started_us = monotonic_us();
        last_wait_log_us = session_started_us;
        if (!initialize_audio()) {
            running.store(false);
            shutdown_audio();
            return;
        }
        while (running.load()) {
            if (reset_epoch_requested.exchange(false))
                reset_media_epoch("transport_gap");
            Packet packet;
            if (!take_next(packet)) {
                if (output_ready.load()) {
                    reclaim_output_buffers();
                    (void)ensure_output_started();
                }
                const uint64_t now_us = monotonic_us();
                if (packets_rx.load() == 0 && now_us - last_wait_log_us >= 5000000) {
                    log("WAIT no_audio_rtp elapsedMs=" +
                        std::to_string((now_us - session_started_us) / 1000));
                    last_wait_log_us = now_us;
                }
                continue;
            }
            // A producer may announce a new SSRC while we were extracting.
            // Never feed a stale packet into the fresh decoder epoch.
            if (packet.epoch != media_epoch.load() || reset_epoch_requested.load())
                continue;
            if (packet.payload.empty())
                submit_plc();
            else
                decode_packet(packet);
        }
        log("SUMMARY rx=" + std::to_string(packets_rx.load()) +
            " decoded=" + std::to_string(packets_decoded.load()) +
            " samples=" + std::to_string(samples_decoded.load()) +
            " gaps=" + std::to_string(sequence_gaps.load()) +
            " late=" + std::to_string(late_drops.load()) +
                " drops=" + std::to_string(queue_drops.load()) +
                " outputThrottle=" + std::to_string(output_throttle_drops.load()) +
            " errors=" + std::to_string(decode_errors.load()) +
            " plc=" + std::to_string(plc_frames.load()) +
            " underruns=" + std::to_string(underruns.load()) +
            " softEmptyResumes=" + std::to_string(empty_queue_grace.resumed) +
            " softEmptyUs/Max=" + std::to_string(empty_queue_grace.silence_us) + "/" + std::to_string(empty_queue_grace.maximum_us) +
            " rebufferUs/Max=" + std::to_string(rebuffer_us_total) + "/" + std::to_string(rebuffer_us_max) +
            " playedCounterResets=" + std::to_string(played_counter_resets.load()) +
            " outputQueueHigh=" + std::to_string(output_queue_high_water.load()) +
            " red/recovered=" + std::to_string(red_packets.load()) + "/" +
            std::to_string(red_recoveries.load()) +
            " epochResets=" + std::to_string(epoch_resets.load()) +
            " inputPeak/Rms=" + std::to_string(last_input_peak.load()) + "/" +
            std::to_string(last_input_rms.load()) +
            " outputPeak/Rms=" + std::to_string(last_output_peak.load()) + "/" +
            std::to_string(last_output_rms.load()) +
            " gainX100=" + std::to_string(last_applied_gain_x100.load()) +
            " limitedFrames=" + std::to_string(limited_frames.load()));
        shutdown_audio();
    }
};

AudioPipeline::AudioPipeline() : impl_(std::make_unique<Impl>()) {}
AudioPipeline::~AudioPipeline() { stop(); }

void AudioPipeline::configure(int volume_percent, int target_buffer_ms) {
    impl_->volume_percent = opennow::audio::ClampVolumeBoostPercent(volume_percent);
    impl_->prime_packets = opennow::audio::PrimePacketCount(target_buffer_ms);
    impl_->output_start_buffers = impl_->prime_packets;
    impl_->start_target_ms.store(impl_->output_start_buffers * 10);
    impl_->maximum_output_start_buffers =
        opennow::audio::MaximumAdaptiveStartBuffers(target_buffer_ms);
    impl_->initial_jitter_hold_us = opennow::audio::InitialJitterHoldUs(target_buffer_ms);
    impl_->resync_jitter_hold_us = opennow::audio::ResyncJitterHoldUs(target_buffer_ms);
    impl_->output_queue_limit = opennow::audio::OutputQueueLimit(target_buffer_ms);
}

bool AudioPipeline::start() {
    if (impl_->running.load())
        return true;
    if (impl_->worker.joinable())
        impl_->worker.join();
    impl_->output_start_buffers = impl_->prime_packets;
    impl_->start_target_ms.store(impl_->output_start_buffers * 10);
    impl_->last_underrun_us = impl_->underrun_episode_start_us = impl_->underrun_restart_us = 0;
    impl_->reserve_release = {};
    impl_->short_gap_reserve = {};
    impl_->empty_queue_grace = {};
    impl_->rebuffer_us_total = impl_->rebuffer_us_max = 0;
    impl_->underruns_in_episode = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->packets.clear();
        impl_->arrival_jitter = {};
        impl_->arrival_jitter_us.store(0);
        impl_->expected_sequence = 0;
        impl_->have_expected = false;
        impl_->primed = false;
        impl_->jitter_hold_until_us = 0;
        impl_->missing_packet_since_us = 0;
    }
    impl_->reset_epoch_requested.store(false);
    impl_->last_receive_us.store(0);
    impl_->submitted_samples.store(0);
    impl_->played_samples.store(0);
    impl_->played_samples_observed_at_us.store(0);
    impl_->last_raw_played_samples = 0;
    impl_->accumulated_played_samples = 0;
    impl_->have_raw_played_samples = false;
    impl_->base_timestamp.store(0);
    impl_->have_base_timestamp.store(false);
    impl_->audio_ssrc.store(0);
    impl_->have_sender_report.store(false);
    impl_->packets_rx.store(0);
    impl_->packets_decoded.store(0);
    impl_->decode_attempts.store(0);
    impl_->samples_decoded.store(0);
    impl_->sequence_gaps.store(0);
    impl_->late_drops.store(0);
    impl_->queue_drops.store(0);
    impl_->output_throttle_drops.store(0);
    impl_->decode_errors.store(0);
    impl_->plc_frames.store(0);
    impl_->underruns.store(0);
    impl_->played_counter_resets.store(0);
    impl_->output_queue_high_water.store(0);
    impl_->red_packets.store(0);
    impl_->red_recoveries.store(0);
    impl_->epoch_resets.store(0);
    impl_->applied_gain = static_cast<float>(impl_->volume_percent) / 100.0f;
    impl_->gain_envelope = {impl_->applied_gain, 0};
    impl_->fade_frames_processed = 0;
    impl_->concealment_recovery_frames_remaining = 0;
    impl_->last_output_samples.fill(0);
    impl_->last_input_peak.store(0);
    impl_->last_input_rms.store(0);
    impl_->last_output_peak.store(0);
    impl_->last_output_rms.store(0);
    impl_->last_applied_gain_x100.store(0);
    impl_->limited_frames.store(0);
    impl_->running.store(true);
    impl_->log("SESSION start");
    impl_->worker = std::thread(&Impl::run, impl_.get());
    return true;
}

void AudioPipeline::stop() {
    if (!impl_ || !impl_->running.exchange(false)) {
        if (impl_ && impl_->worker.joinable())
            impl_->worker.join();
        return;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable())
        impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->packets.clear();
}

void AudioPipeline::submit(const PeerAudioPacket& source) {
    if (!impl_->running.load() || !source.data || source.size == 0)
        return;
    const uint32_t previous_ssrc = impl_->audio_ssrc.exchange(source.ssrc);
    const bool ssrc_changed = previous_ssrc != 0 && previous_ssrc != source.ssrc;
    if (ssrc_changed) {
        impl_->have_sender_report.store(false, std::memory_order_release);
        impl_->have_base_timestamp.store(false, std::memory_order_release);
    }
    const uint64_t now_us = monotonic_us();
    const uint64_t previous_receive_us = impl_->last_receive_us.exchange(now_us);
    Impl::Packet packet;
    packet.payload.assign(source.data, source.data + source.size);
    packet.timestamp = source.timestamp;
    packet.ssrc = source.ssrc;
    packet.sequence = source.sequence;
    packet.payload_type = source.payload_type;
    packet.arrival_us = now_us;
    impl_->packets_rx++;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (ssrc_changed) {
        impl_->packets.clear();
        impl_->arrival_jitter = {};
        impl_->arrival_jitter_us.store(0);
        impl_->primed = false;
        impl_->have_expected = false;
        impl_->missing_packet_since_us = 0;
        impl_->jitter_hold_until_us = now_us + impl_->resync_jitter_hold_us;
        impl_->reset_epoch_requested.store(true);
        impl_->media_epoch.fetch_add(1);
    }
    if (impl_->jitter_hold_until_us == 0 && !impl_->primed)
        impl_->jitter_hold_until_us = now_us + impl_->initial_jitter_hold_us;
    if (opennow::audio::ShouldResetEpoch(previous_receive_us, now_us, impl_->have_expected,
                                          impl_->expected_sequence, packet.sequence, kMaxPackets)) {
        impl_->packets.clear();
        impl_->primed = false;
        impl_->have_expected = false;
        impl_->missing_packet_since_us = 0;
        impl_->jitter_hold_until_us = now_us + impl_->resync_jitter_hold_us;
        impl_->reset_epoch_requested.store(true);
        impl_->media_epoch.fetch_add(1);
    }
    packet.epoch = impl_->media_epoch.load();
    if (impl_->have_expected && int16_t(packet.sequence - impl_->expected_sequence) < 0) {
        impl_->late_drops++;
        return;
    }
    const auto duplicate = std::find_if(
        impl_->packets.begin(), impl_->packets.end(), [&packet](const Impl::Packet& item) {
            return item.ssrc == packet.ssrc && item.sequence == packet.sequence;
        });
    if (duplicate != impl_->packets.end())
        return;
    impl_->arrival_jitter.Observe(packet.sequence, packet.timestamp, packet.arrival_us);
    impl_->arrival_jitter_us.store(impl_->arrival_jitter.jitter_us, std::memory_order_relaxed);
    if (impl_->packets.size() >= kMaxPackets) {
        impl_->packets.pop_front();
        impl_->queue_drops++;
    }
    auto position = std::find_if(impl_->packets.begin(), impl_->packets.end(), [&packet](const Impl::Packet& item) {
        return int16_t(packet.sequence - item.sequence) < 0;
    });
    impl_->packets.insert(position, std::move(packet));
    impl_->cv.notify_one();
}

void AudioPipeline::set_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp) {
    if (impl_->audio_ssrc.load() == 0 || impl_->audio_ssrc.load() != ssrc)
        return;
    {
        std::lock_guard<std::mutex> lock(impl_->sender_report_mutex);
        if (impl_->audio_ssrc.load() != ssrc)
            return;
        impl_->sr_ntp_us = ntp_us;
        impl_->sr_rtp_timestamp = rtp_timestamp;
        impl_->have_sender_report.store(true, std::memory_order_release);
    }
    impl_->log("RTCP_SR ssrc=" + std::to_string(ssrc) + " ntpUs=" + std::to_string(ntp_us) +
               " rtp=" + std::to_string(rtp_timestamp));
}

int64_t AudioPipeline::playback_ntp_us() const {
    if (!impl_->output_ready.load() || !impl_->have_base_timestamp.load() ||
        !impl_->have_sender_report.load(std::memory_order_acquire))
        return -1;
    // A dropped decoded block creates a discontinuity between the physical
    // sample counter and RTP content. Fall back to R5's source pacing instead
    // of feeding an increasingly wrong audio deadline to video presentation.
    if (impl_->queue_drops.load(std::memory_order_relaxed) != 0)
        return -1;
    const uint64_t observed_at_us =
        impl_->played_samples_observed_at_us.load(std::memory_order_acquire);
    const uint64_t clock_us = impl_->output_started.load(std::memory_order_acquire)
        ? monotonic_us() : observed_at_us;
    const uint64_t played = opennow::audio::EstimatePlayedSamples(
        impl_->played_samples.load(std::memory_order_relaxed), observed_at_us,
        clock_us, impl_->submitted_samples.load(std::memory_order_relaxed),
        kSampleRate);
    const uint32_t playback_rtp = static_cast<uint32_t>(impl_->base_timestamp.load() + played);
    std::lock_guard<std::mutex> lock(impl_->sender_report_mutex);
    if (!impl_->have_sender_report.load(std::memory_order_acquire))
        return -1;
    return static_cast<int64_t>(impl_->sr_ntp_us) +
           opennow::audio::RtpDeltaToUs(
               playback_rtp, impl_->sr_rtp_timestamp, kSampleRate);
}

std::string AudioPipeline::debug_info() const {
    size_t queue_size = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        queue_size = impl_->packets.size();
    }
    const uint64_t played = impl_->played_samples.load();
    const uint64_t submitted = impl_->submitted_samples.load();
    const uint64_t queued = submitted > played ? submitted - played : 0;
    char text[430];
    std::snprintf(text, sizeof(text),
        "Audio rx/decoded/errors: %llu/%llu/%llu\nAudio queue packets/ms: %zu/%llu  gaps/late/drop: %llu/%llu/%llu\nAudio samples played/queued PLC/underrun RED/recovered/reset: %llu/%llu %llu/%llu %llu/%llu/%llu\nAudio level in peak/rms -> out peak/rms, gain/limit: %d/%d -> %d/%d, %.2fx/%llu",
        static_cast<unsigned long long>(impl_->packets_rx.load()),
        static_cast<unsigned long long>(impl_->packets_decoded.load()),
        static_cast<unsigned long long>(impl_->decode_errors.load()), queue_size,
        static_cast<unsigned long long>(queued * 1000 / kSampleRate),
        static_cast<unsigned long long>(impl_->sequence_gaps.load()),
        static_cast<unsigned long long>(impl_->late_drops.load()),
        static_cast<unsigned long long>(impl_->queue_drops.load()),
        static_cast<unsigned long long>(played), static_cast<unsigned long long>(queued),
        static_cast<unsigned long long>(impl_->plc_frames.load()),
        static_cast<unsigned long long>(impl_->underruns.load()),
        static_cast<unsigned long long>(impl_->red_packets.load()),
        static_cast<unsigned long long>(impl_->red_recoveries.load()),
        static_cast<unsigned long long>(impl_->epoch_resets.load()),
        impl_->last_input_peak.load(), impl_->last_input_rms.load(),
        impl_->last_output_peak.load(), impl_->last_output_rms.load(),
        static_cast<double>(impl_->last_applied_gain_x100.load()) / 100.0,
        static_cast<unsigned long long>(impl_->limited_frames.load()));
    return text;
}

AudioPerformanceCounters AudioPipeline::performance() const {
    AudioPerformanceCounters result;
    if (!impl_) return result;
    result.plc = impl_->plc_frames.load(std::memory_order_relaxed);
    result.underruns = impl_->underruns.load(std::memory_order_relaxed);
    result.errors = impl_->decode_errors.load(std::memory_order_relaxed);
    result.arrival_jitter_us = impl_->arrival_jitter_us.load(std::memory_order_relaxed);
    result.start_target_ms = impl_->start_target_ms.load(std::memory_order_relaxed);
    result.limited_blocks = impl_->limited_frames.load(std::memory_order_relaxed);
    result.gain_x100 = impl_->last_applied_gain_x100.load(std::memory_order_relaxed);
    const auto played = impl_->played_samples.load(std::memory_order_relaxed);
    const auto submitted = impl_->submitted_samples.load(std::memory_order_relaxed);
    result.queued_ms = submitted > played ? (submitted - played) * 1000 / kSampleRate : 0;
    return result;
}
