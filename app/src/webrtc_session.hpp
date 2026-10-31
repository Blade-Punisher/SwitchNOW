#pragma once
#include "models.hpp"
#include "stream_settings.hpp"
#include "stream_adaptation_policy.hpp"
#include "frame_delivery_policy.hpp"
#include "ingress_timing_policy.hpp"
#include "input_scheduling_policy.hpp"
#include "signaling_diagnostics.hpp"
#include "signaling_registration.hpp"
#include "interval_maximum.hpp"
#include "stream_end_policy.hpp"
#include "signaling_client.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include "peer_connection.h"
#include "peer.h"
#include "stream/ffmpeg/FFmpegVideoDecoder.hpp"
#include "stream/ffmpeg/DecodedFrameGate.hpp"
#include "stream/IVideoRenderer.hpp"
#if defined(USE_DK_RENDERER)
#include "stream/deko3d/DKVideoRenderer.hpp"
#else
#include "stream/OpenGL/GLVideoRenderer.hpp"
#endif
#include "stream/audio/AudioPipeline.hpp"

typedef struct json_t json_t;

struct VideoPerformanceCounters {
    uint64_t access_units = 0;
    uint64_t access_unit_bytes = 0;
    uint64_t decoded_frames = 0;
    uint64_t presented_frames = 0;
    uint64_t decode_us_p95 = 0;
    uint64_t decode_us_max = 0;
    uint64_t queue_wait_us_p95 = 0;
    uint64_t queue_wait_us_max = 0;
    uint64_t render_us_p95 = 0;
    uint64_t render_us_max = 0;
    size_t decode_queue_size = 0;
    size_t decode_queue_high_water = 0;
    uint64_t arrival_p95_us = 0, arrival_jitter_us = 0;
    uint64_t arrival_over33 = 0, arrival_over50 = 0, arrival_over100 = 0;
    uint64_t repeated_frames = 0, presentation_drops = 0;
    uint64_t ready_count = 0, ready_age_total_us = 0, ready_depth3 = 0, latency_trims = 0, latency_backoffs = 0;
};

struct StreamTransportHealth {
    bool offer_received = false, startup_timed_out = false;
    bool input_ready = false;
    bool peer_completed = false;
    opennow::PeerTerminalKind peer_terminal = opennow::PeerTerminalKind::None;
    bool signaling_connected = false;
    bool video_started = false;
    std::chrono::milliseconds video_idle {0};
};

struct StreamNetworkCounters {
    std::uint32_t packets_received = 0;
    std::uint32_t sequence_gaps = 0;
    std::uint32_t late_packets_dropped = 0;
    std::uint32_t access_units_dropped = 0;
    std::uint32_t nack_requests = 0;
    std::uint32_t reordered = 0, rtx_recovered = 0;
};
struct ClientPerformanceCounters {
    std::uint64_t input_queue_max_us = 0, input_send_max_us = 0;
    std::uint64_t transport_batch_max_us = 0, input_backpressure = 0;
    std::uint64_t input_interval_max_us = 0;
    std::uint64_t input_queue_window_us = 0, input_send_window_us = 0, transport_batch_window_us = 0;
    std::uint64_t transport_service_window_us = 0;
    std::uint64_t live_maintenance_window_us = 0;
    std::uint64_t live_maintenance_deferrals = 0, input_idle_wait_skips = 0;
    AudioPerformanceCounters audio;
};

enum class NetworkRttSource {
    None,
    Stun,
    Sctp,
};

struct NetworkRttStats {
    int snapshot_age_ms = -1;
    bool snapshot_fresh = false;
    int current_ms = -1;
    int stun_current_ms = -1;
    int sctp_current_ms = -1;
    int min_ms = -1;
    int median_ms = -1;
    int p95_ms = -1;
    int jitter_ms = -1;
    std::uint32_t stun_sample_age_ms = 0;
    std::uint32_t samples = 0;
    std::uint32_t probes_sent = 0;
    std::uint32_t probes_matched = 0;
    std::uint32_t probes_lost = 0;
    std::uint32_t probes_unmatched = 0;
    bool probe_in_flight = false;
    NetworkRttSource source = NetworkRttSource::None;
};

struct LivePauseStats {
    uint64_t transport_count = 0;
    uint64_t transport_duration_ms = 0;
    uint64_t source_count = 0;
    uint64_t assembly_count = 0;
    uint64_t playback_count = 0;
    uint64_t playback_duration_ms = 0;
    uint64_t uncertain_count = 0;
};

class WebRtcSession {
public:
    WebRtcSession(
        const std::string& signaling_url,
        const std::string& jwt_token,
        const std::string& session_id,
        const std::string& media_ip,
        int media_port,
        const std::vector<opennow::IceServerInfo>& ice_servers,
        const opennow::StreamSettings& settings = opennow::LoadStreamSettings(),
        opennow::gfn::cloud_session::ClientProfile profile = opennow::gfn::cloud_session::ClientProfile::NativeWebRtc);
    ~WebRtcSession();

    void start();
    void request_stop();
    void stop();
    void poll(bool transport_owner = false);
    bool send_gamepad_input(uint8_t controller_id, uint16_t controller_bitmap,
                            uint16_t buttons, uint8_t left_trigger, uint8_t right_trigger,
                            float lx, float ly, float rx, float ry);
    void send_mouse_move(int16_t dx, int16_t dy);
    void send_mouse_left_button(bool pressed);
    bool send_keyboard_key(uint16_t keycode, uint16_t scancode, uint16_t modifiers, bool pressed);
    const char* video_decoder_label() const {
        if (decoder_setup_result_ != 0) return "Unavailable";
        return video_backend_name_.rfind("NVDEC", 0) == 0 ? "NVDEC" : "Software";
    }
    int stream_width() const {
        const int rendered = rendered_video_width_.load(std::memory_order_relaxed);
        return rendered > 0 ? rendered : settings_.width;
    }
    int stream_height() const {
        const int rendered = rendered_video_height_.load(std::memory_order_relaxed);
        return rendered > 0 ? rendered : settings_.height;
    }
    void draw(NVGcontext* vg, int width, int height, AVFrame* frame, uint64_t generation);

    // Callbacks from libpeer
    void on_ice_candidate(const std::string& sdp);
    void on_peer_state_change(PeerConnectionState state);
    void on_datachannel_message(const char* msg, size_t len, uint16_t sid);
    void on_datachannel_open();
    void on_datachannel_close();
    void on_video_packet(const PeerVideoPacket& packet);
    void on_audio_packet(const PeerAudioPacket& packet);
    void on_rtp_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp);
    int64_t video_target_rtp_timestamp() const;
    VideoPerformanceCounters get_video_performance() const;
    ClientPerformanceCounters get_client_performance() const;
    uint64_t diagnostic_attempt_id() const { return diagnostic_attempt_id_; }
    int get_network_rtt_ms() const;
    std::string get_adaptation_info() const;
    NetworkRttStats get_network_rtt_stats(bool transport_snapshot = false) const;
    LivePauseStats get_live_pause_stats() const {
        return {transport_pause_count_.load(), transport_pause_us_.load() / 1000,
                source_pause_count_.load(), assembly_pause_count_.load(),
                playback_pause_count_.load(), playback_pause_us_.load() / 1000,
                uncertain_pause_count_.load()};
    }
    StreamNetworkCounters get_network_counters() const;
    StreamTransportHealth get_transport_health() const;
    std::string signaling_endpoint() const { return signaling_url_; }
    std::string get_debug_info() const;
    bool is_terminal() const { return peer_terminal_.load(std::memory_order_acquire); }
    void record_ui_event(const std::string& event);
    void note_recovery_end(bool deadline);

private:
    std::string signaling_url_;
    opennow::gfn::cloud_session::ClientProfile cloud_client_profile_;
    std::string jwt_token_;
    std::string session_id_;
    std::string media_ip_;
    int media_port_ = 0;
    std::vector<opennow::IceServerInfo> ice_servers_;
    opennow::StreamSettings settings_;
    std::string peer_name_;
    int peer_id_ = 0;
    int remote_peer_id_ = 1;
    int ack_counter_ = 0;
    bool signaling_ready_ = false;
    bool remote_description_set_ = false;
    std::atomic<bool> offer_received_ {false}, startup_timed_out_ {false};
    uint64_t diagnostic_attempt_id_ = 0;
    bool manual_candidate_added_ = false;
    bool remote_trickle_received_ = false;
    bool answer_sent_ = false;
    std::unique_ptr<SignalingClient> signaling_client_;
    struct PeerConnection* pc_ = nullptr;
    std::unique_ptr<FFmpegVideoDecoder> decoder_;
    std::unique_ptr<IVideoRenderer> renderer_;
    int renderer_frame_format_ = -1;
    std::atomic<int> rendered_video_width_ {0};
    std::atomic<int> rendered_video_height_ {0};
    std::unique_ptr<AudioPipeline> audio_;
    int decoder_setup_result_ = -1;
    bool decoder_fallback_used_ = false;
    bool auto_safe_mode_used_ = false;
    bool nvdec_marker_owned_ = false;
    std::string video_backend_name_ = "uninitialized";
    std::string decoder_setup_diagnostics_;
    std::string nvdec_setup_diagnostics_;
    std::thread network_thread_;
    std::atomic<bool> network_running_ {false};
    std::atomic<bool> stop_requested_ {false};
    std::atomic<bool> peer_terminal_ {false};
    std::atomic<bool> peer_ever_completed_ {false};
    std::atomic<int> peer_terminal_kind_ {
        static_cast<int>(opennow::PeerTerminalKind::None)};
    mutable std::recursive_mutex peer_mutex_;
    opennow::signaling::Diagnostics signaling_diagnostics_;
    opennow::signaling::RegistrationPolicy signaling_registration_;
    std::atomic<opennow::signaling::EndReason> signaling_end_reason_ {opennow::signaling::EndReason::None};
    struct DecodeUnit {
        std::vector<uint8_t> data;
        bool idr = false;
        uint32_t rtp_timestamp = 0;
        std::chrono::steady_clock::time_point enqueued_at {};
        bool reset_decoder_before_decode = false;
    };
    std::thread decoder_thread_;
    std::atomic<bool> decoder_running_ {false};
    // A newly joined H.264 stream has no reference picture. Dependent P frames
    // must not reach the decoder until the first IDR establishes that state.
    opennow::video::DecodedFrameGate video_frame_gate_;
    std::atomic<bool> decoder_reset_requested_ {false};
    std::atomic<bool> keyframe_needed_ {false};
    std::atomic<unsigned> recovery_pli_retries_ {0};
    uint64_t video_at_last_keyframe_request_us_ = 0;
    std::atomic<uint64_t> recovery_started_us_ {0};
    std::atomic<uint64_t> recovery_present_started_us_ {0};
    std::atomic<uint64_t> recovery_present_max_us_ {0};
    opennow::streaming::Adaptation adaptation_;
    uint64_t adaptation_sample_at_us_ = 0;
    uint64_t adaptation_previous_units_ = 0, adaptation_previous_decoded_ = 0;
    uint32_t adaptation_previous_damage_ = 0;
    uint64_t adaptation_previous_transport_pauses_ = 0;
    uint64_t adaptation_previous_playback_pauses_ = 0;
    uint64_t last_rtt_publish_us_ = 0;
    int previous_adaptation_wait_us_ = -1;
    opennow::streaming::LivePauseTracker live_pause_tracker_;
    uint64_t pause_sample_at_us_ = 0;
    std::atomic<uint64_t> transport_pause_count_ {0};
    std::atomic<uint64_t> transport_pause_us_ {0};
    std::atomic<uint64_t> source_pause_count_ {0};
    std::atomic<uint64_t> assembly_pause_count_ {0};
    std::atomic<uint64_t> playback_pause_count_ {0};
    std::atomic<uint64_t> playback_pause_us_ {0};
    std::atomic<uint64_t> uncertain_pause_count_ {0};
    std::atomic<int> adaptation_pressure_ {0};
    std::atomic<int> adaptation_wait_us_ {3000};
    std::atomic<uint64_t> recovery_duration_us_max_ {0};
    std::atomic<uint64_t> recovery_duration_us_total_ {0};
    std::atomic<uint64_t> recovery_duration_count_ {0};
    std::atomic<uint64_t> input_lock_us_max_ {0};
    std::atomic<uint64_t> input_send_us_max_ {0};
    std::atomic<uint64_t> input_interval_us_max_ {0};
    uint64_t last_input_attempt_us_ = 0;
    uint64_t last_live_maintenance_us_ = 0;
    std::atomic<uint64_t> live_maintenance_window_max_us_ {0};
    opennow::performance::IntervalMaximum live_maintenance_performance_window_;
    std::atomic<uint64_t> live_maintenance_deferrals_ {0}, input_idle_wait_skips_ {0};
    mutable std::mutex render_snapshot_mutex_;
    VideoRenderStats render_stats_snapshot_{};
    VideoRenderStats last_reported_render_stats_{};
    mutable std::mutex debug_snapshot_mutex_;
    mutable std::string debug_snapshot_;
    mutable uint64_t debug_snapshot_at_us_ = 0;
    mutable std::mutex decoder_queue_mutex_;
    std::condition_variable decoder_queue_cv_;
    std::deque<DecodeUnit> decoder_queue_;
    std::deque<std::vector<uint8_t>> decoder_buffer_pool_;
    std::atomic<int> decoder_queue_drops_ {0};
    std::atomic<uint64_t> decoder_resync_drops_ {0};
    std::atomic<uint64_t> decoder_overflow_drops_ {0};
    std::atomic<uint64_t> decoder_stall_drops_ {0};
    std::atomic<uint64_t> decoder_buffer_reuses_ {0};
    std::atomic<uint64_t> decoder_buffer_allocations_ {0};
    std::atomic<uint64_t> transport_batches_ {0};
    std::atomic<uint64_t> transport_datagrams_ {0};
    std::atomic<size_t> transport_batch_high_water_ {0};
    std::atomic<bool> interactive_input_pending_ {false};
    struct PendingGamepadReport {
        std::vector<uint8_t> wire_payload;
        uint16_t sid = 0;
        uint8_t controller_id = 0;
        uint64_t queued_at_us = 0;
        uint64_t attempt = 0;
        uint32_t buttons = 0; // local edge key includes both triggers
    };
    mutable std::mutex outbound_input_mutex_;
    std::condition_variable network_wake_cv_;
    std::atomic<uint64_t> input_wake_generation_ {0};
    std::array<std::deque<PendingGamepadReport>, 4> pending_gamepad_reports_ {};
    size_t next_gamepad_report_ = 0;
    std::atomic<bool> input_delivery_ready_ {false};
    std::atomic<uint64_t> input_queue_wait_us_max_ {0};
    opennow::performance::IntervalMaximum input_queue_window_, input_send_window_, transport_batch_window_;
    opennow::performance::IntervalMaximum transport_service_window_;
    std::array<std::atomic<uint64_t>, 10> input_queue_wait_buckets_ {};
    std::array<std::atomic<uint64_t>, 10> input_send_buckets_ {};
    std::atomic<uint64_t> gamepad_reports_coalesced_ {0};
    uint64_t input_backpressure_until_us_ = 0;
    std::atomic<uint64_t> input_backpressure_count_ {0};
    std::atomic<uint64_t> gamepad_button_edges_dropped_ {0};
    std::atomic<uint64_t> ui_poll_lock_skips_ {0};
    mutable std::atomic<uint64_t> video_clock_lock_skips_ {0};
    mutable std::atomic<uint64_t> video_clock_no_video_sr_ {0}, video_clock_no_audio_ {0}, video_clock_available_ {0};
    mutable std::atomic<uint64_t> stats_lock_skips_ {0};
    mutable std::atomic<int> cached_network_rtt_ms_ {-1};
    mutable std::mutex rtt_snapshot_mutex_;
    mutable NetworkRttStats rtt_snapshot_;
    mutable uint64_t rtt_snapshot_at_us_ = 0;
    mutable std::atomic<std::uint32_t> cached_rtp_packets_ {0};
    mutable std::atomic<std::uint32_t> cached_rtp_gaps_ {0};
    mutable std::atomic<std::uint32_t> cached_rtp_late_ {0};
    mutable std::atomic<std::uint32_t> cached_rtp_au_dropped_ {0};
    mutable std::atomic<std::uint32_t> cached_rtp_nack_requests_ {0};
    mutable std::atomic<std::uint32_t> cached_rtp_reordered_ {0}, cached_rtp_rtx_ {0};

    std::string current_state_ = "Initializing";
    std::atomic<int> packets_received_ {0};
    std::atomic<int> frames_decoded_ {0};
    std::atomic<int> decode_errors_ {0};
    std::atomic<int64_t> last_rendered_video_pts_ {AV_NOPTS_VALUE};
    std::atomic<uint64_t> video_access_unit_bytes_ {0};
    std::atomic<uint64_t> video_access_unit_max_bytes_ {0};
    std::atomic<uint64_t> presented_frames_ {0};
    std::atomic<uint64_t> last_presented_generation_ {0};
    std::atomic<uint64_t> decode_us_total_ {0};
    std::atomic<uint64_t> decode_us_max_ {0};
    std::atomic<uint64_t> queue_wait_us_max_ {0};
    std::atomic<uint64_t> render_us_total_ {0};
    std::atomic<uint64_t> render_us_max_ {0};
    std::atomic<uint64_t> render_late_frames_ {0};
    std::atomic<size_t> decoder_queue_high_water_ {0};
    std::array<std::atomic<uint64_t>, 10> decode_latency_buckets_ {};
    std::array<std::atomic<uint64_t>, 10> queue_wait_buckets_ {};
    std::array<std::atomic<uint64_t>, 10> render_latency_buckets_ {};
    bool initial_keyframe_requested_ = false;
    int keyframe_request_attempts_ = 0;
    int last_decode_stall_request_packet_ = 0;
    int last_decode_stall_frame_count_ = 0;
    int signaling_rx_count_ = 0;
    int offer_count_ = 0;
    int remote_ice_count_ = 0;
    int local_ice_count_ = 0;
    int heartbeat_rx_count_ = 0;
    int heartbeat_tx_count_ = 0;
    bool peer_completed_seen_ = false;
    bool datachannel_opened_ = false;
    bool datachannel_open_requested_ = false;
    bool reliable_input_channel_requested_ = false;
    bool partial_input_channel_requested_ = false;
    std::atomic<bool> input_ready_ {false};
    bool startup_control_sent_ = false;
    int datachannel_open_attempts_ = 0;
    std::atomic<int> input_protocol_version_ {2};
    int partial_reliable_threshold_ms_ = 16;
    int input_heartbeat_tx_count_ = 0;
    int gamepad_tx_count_ = 0;
    int gamepad_input_attempt_count_ = 0;
    int gamepad_input_blocked_count_ = 0;
    int gamepad_send_failure_count_ = 0;
    int consecutive_input_send_failures_ = 0;
    int mouse_tx_count_ = 0;
    std::array<uint16_t, 4> gamepad_sequences_ {1, 1, 1, 1};
    bool first_video_packet_logged_ = false;
    bool first_decoded_frame_logged_ = false;
    int last_logged_packet_count_ = 0;
    std::atomic<int> last_logged_frame_count_ {0};
    std::atomic<int> last_logged_decode_error_count_ {0};
    std::chrono::steady_clock::time_point last_heartbeat_sent_ {};
    std::chrono::steady_clock::time_point session_started_at_ {};
    std::chrono::steady_clock::time_point peer_completed_at_ {};
    std::chrono::steady_clock::time_point last_stream_diagnostic_log_ {};
    std::string last_ice_candidate_pair_signature_;
    std::chrono::steady_clock::time_point last_video_metric_snapshot_ {};
    VideoPerformanceCounters last_logged_video_performance_ {};
    std::chrono::steady_clock::time_point last_startup_keyframe_request_ {};
    std::atomic<uint64_t> last_decoded_frame_at_us_ {0};
    std::atomic<uint64_t> last_video_packet_at_us_ {0};
    std::atomic<uint64_t> last_audio_packet_at_us_ {0};
    std::chrono::steady_clock::time_point last_decode_stall_request_at_ {};
    std::chrono::steady_clock::time_point last_rtp_damage_request_at_ {};
    std::chrono::steady_clock::time_point last_keyframe_request_at_ {};
    std::chrono::steady_clock::time_point last_datachannel_open_attempt_ {};
    std::chrono::steady_clock::time_point last_input_heartbeat_sent_ {};
    std::chrono::steady_clock::time_point input_activation_due_ {};
    std::chrono::steady_clock::time_point manual_candidate_due_ {};
    std::vector<std::string> pending_manual_candidates_;
    std::string server_ice_ufrag_;
    std::vector<std::string> pending_local_candidates_;
    std::vector<std::string> last_messages_;
    struct SenderReport {
        uint32_t ssrc = 0;
        uint64_t ntp_us = 0;
        uint32_t rtp_timestamp = 0;
    };
    std::vector<SenderReport> sender_reports_;
    uint32_t audio_ssrc_ = 0;
    uint32_t video_ssrc_ = 0;
    uint64_t video_sr_ntp_us_ = 0;
    uint32_t video_sr_rtp_timestamp_ = 0;
    std::atomic_bool have_video_sender_report_ {false};
    uint32_t last_rtp_access_units_dropped_ = 0;
    std::atomic<uint32_t> last_rtp_damage_gate_ {0};
    std::atomic<uint32_t> last_decoded_idr_timestamp_ {0};
    std::atomic_bool has_decoded_idr_ {false};
    uint32_t last_video_au_timestamp_ = 0;
    uint64_t last_video_au_arrival_us_ = 0;
    bool has_video_au_arrival_ = false;
    opennow::streaming::FrameDelivery frame_delivery_; // transport thread only
    std::atomic<uint64_t> arrival_p95_us_ {0}, arrival_jitter_us_ {0};
    std::atomic<uint64_t> arrival_over33_ {0}, arrival_over50_ {0}, arrival_over100_ {0};
    std::atomic<uint64_t> source_timestamp_gaps_ {0};
    std::atomic<uint64_t> timestamp_gaps_with_loss_ {0};
    uint32_t last_arrival_damage_count_ = 0;
    std::atomic<uint64_t> delivery_arrival_gaps_ {0};
    std::atomic<uint64_t> delivery_bursts_ {0};
    std::atomic<uint64_t> input_sctp_us_max_ {0};
    std::atomic<uint64_t> gamepad_send_window_max_us_ {0}, heartbeat_send_window_max_us_ {0};
    std::atomic<uint64_t> ui_maintenance_window_max_us_ {0}, input_priority_ui_deferrals_ {0};
    uint64_t last_input_ui_maintenance_us_ = 0; // UI thread only
    std::atomic<uint64_t> transport_batch_us_max_ {0};
    opennow::streaming::IngressTiming ingress_timing_; // peer lock / network thread
    std::atomic<uint64_t> ingress_poll_gap_window_us_ {0}, ingress_lock_wait_window_us_ {0};
    std::atomic<uint64_t> ingress_raw_gap_window_us_ {0}, ingress_assembly_window_us_ {0};
    std::atomic<uint64_t> ingress_callback_window_us_ {0};
    std::uint64_t ingress_last_diagnostic_us_ = 0;

    void handle_signaling_message(const std::string& msg);
    void setup_peer_connection();
    void start_network_worker();
    void network_loop();
    int flush_pending_gamepad_inputs_locked(size_t maximum_reports = 2);
    void clear_pending_gamepad_inputs();
    bool has_pending_gamepad_inputs() const;
    void start_decoder_worker();
    void decoder_loop();
    void enqueue_decode_unit(const uint8_t* data, size_t size, uint32_t rtp_timestamp);
    void recycle_decode_buffer_locked(std::vector<uint8_t>&& buffer);
    void clear_decoder_queue_locked();
    void send_peer_info();
    void send_peer_payload(json_t* payload);
    void send_heartbeat();
    void maybe_send_keepalive();
    void queue_local_candidates_from_sdp(const std::string& sdp);
    void send_local_candidate(const std::string& candidate);
    void flush_pending_local_candidates();
    void schedule_manual_media_candidates(const std::string& offer_sdp, const std::string& nvst_sdp);
    void maybe_add_manual_media_candidate();
    void maybe_log_stream_diagnostics();
    void maybe_open_datachannel();
    void reset_input_channel_for_retry(const char* reason);
    void note_input_send_result(int sent, const char* label);
    void maybe_send_startup_control_messages();
    void send_datachannel_text(const std::string& label, const std::string& payload);
    int send_datachannel_binary(uint16_t sid, const std::string& label, const uint8_t* payload, size_t size);
    void maybe_send_input_heartbeat();
    void maybe_activate_input();
    void maybe_request_startup_keyframe_retry();
    void maybe_recover_decode_stall();
    void maybe_recover_rtp_damage();
    void log_stream_summary(const char* reason);
    void request_keyframe(const char* reason);
    void begin_video_recovery();
    int next_ack_id();
};
