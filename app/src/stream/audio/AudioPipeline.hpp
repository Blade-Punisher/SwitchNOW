#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct PeerAudioPacket;
struct AudioPerformanceCounters {
    std::uint64_t plc = 0, underruns = 0, errors = 0, queued_ms = 0;
    std::uint64_t arrival_jitter_us = 0, start_target_ms = 0, limited_blocks = 0;
    int gain_x100 = 100;
};

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    bool start();
    void configure(int volume_percent, int target_buffer_ms);
    void stop();
    void submit(const PeerAudioPacket& packet);
    void set_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp);
    std::string debug_info() const;
    AudioPerformanceCounters performance() const;
    int64_t playback_ntp_us() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
