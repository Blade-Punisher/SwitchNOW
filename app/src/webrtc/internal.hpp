#pragma once

#include "peer.h"
#include "peer_connection.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace opennow::webrtc::internal
{

bool StartsWith(const std::string& value, const char* prefix);
void RecordLatency(std::array<std::atomic<uint64_t>, 10>& buckets, uint64_t latency_us);
uint64_t LatencyPercentile95(const std::array<std::atomic<uint64_t>, 10>& buckets);
uint64_t LatencyPercentile99(const std::array<std::atomic<uint64_t>, 10>& buckets);

template <typename T>
void AtomicMax(std::atomic<T>& target, T value)
{
    T previous = target.load(std::memory_order_relaxed);
    while (previous < value &&
           !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
    }
}

std::string HexPreview(const uint8_t* data, size_t size, size_t max_bytes = 12);
bool ContainsH264Idr(const uint8_t* data, size_t size);
uint64_t NowUs();

void AppendInputLog(const std::string& line);
void AppendStreamLog(const std::string& line);
void AppendLibPeerLog(const char* line, bool input_relevant);
uint64_t ResetStreamTraceLog(const std::string& session_id);
void FlushDiagnosticLogs();
void RequestDiagnosticFlush();
void AppendTraceLog(const std::string& line);
void AppendTraceBlock(const std::string& title, const std::string& body);
std::string PreviewText(const std::string& value, size_t max_chars = 160);

bool InputEncodingSelfTest();

extern "C" {
void on_ice_candidate_cb(char* sdp_text, void* userdata);
void on_peer_state_change_cb(PeerConnectionState state, void* userdata);
void on_datachannel_message_cb(char* msg, size_t len, void* userdata, uint16_t sid);
void on_datachannel_open_cb(void* userdata);
void on_datachannel_close_cb(void* userdata);
void on_video_packet_cb(const PeerVideoPacket* packet, void* userdata);
void on_audio_packet_cb(const PeerAudioPacket* packet, void* userdata);
void on_rtp_sender_report_cb(uint32_t ssrc, uint64_t ntp_us,
                             uint32_t rtp_timestamp, void* userdata);
}

} // namespace opennow::webrtc::internal
