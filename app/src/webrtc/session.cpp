#include "stream_maintenance_policy.hpp"
#include "webrtc_session.hpp"
#include "stream_diagnostics.hpp"
#include "network_loop_policy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "borealis/core/logger.hpp"
#include "internal.hpp"
#include "gfn/session_endpoints.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <thread>

#ifdef __SWITCH__
#include <sys/stat.h>
#include <switch.h>
#endif

using namespace opennow::webrtc::internal;


namespace
{

constexpr const char* kNvdecActiveMarker =
    "sdmc:/switch/SwitchNOW/nvdec_active.marker";

// Sampled once per diagnostic interval, never once per packet. Wall time
// includes socket IPC and scheduler preemption; thread ticks measure CPU only.
int64_t WorkerCpuMicros() {
#ifdef __SWITCH__
    u64 ticks = 0;
    Result result = svcGetInfo(&ticks, InfoType_ThreadTickCount, CUR_THREAD_HANDLE, TickCountInfo_Total);
    if (R_FAILED(result))
        result = svcGetInfo(&ticks, InfoType_ThreadTickCountDeprecated, CUR_THREAD_HANDLE, TickCountInfo_Total);
    if (R_SUCCEEDED(result)) return static_cast<int64_t>(armTicksToNs(ticks) / 1000);
#endif
    return -1;
}

bool ConsumeNvdecCrashMarker()
{
    std::ifstream marker(kNvdecActiveMarker, std::ios::binary);
    const bool exists = marker.good();
    marker.close();
    if (exists)
        std::remove(kNvdecActiveMarker);
    return exists;
}

bool CreateNvdecCrashMarker()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
#endif
    std::ofstream marker(kNvdecActiveMarker, std::ios::binary | std::ios::trunc);
    if (!marker.is_open())
        return false;
    marker << "NVDEC stream active; remove after clean shutdown\n";
    marker.flush();
    return marker.good();
}


std::string MakePeerName()
{
    std::random_device rd;
    const auto ticks = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return "peer-" + std::to_string((ticks ^ rd()) % 10000000000ULL);
}

void LibPeerDiagnosticSink(const char* line, int input_relevant)
{
    AppendLibPeerLog(line, input_relevant != 0);
}

} // namespace

WebRtcSession::WebRtcSession(
    const std::string& signaling_url,
    const std::string& jwt_token,
    const std::string& session_id,
    const std::string& media_ip,
    int media_port,
    const std::vector<opennow::IceServerInfo>& ice_servers,
    const opennow::StreamSettings& settings, opennow::gfn::cloud_session::ClientProfile profile)
    : signaling_url_(signaling_url),
      cloud_client_profile_(profile),
      jwt_token_(jwt_token),
      session_id_(session_id),
      media_ip_(media_ip),
      media_port_(media_port),
      ice_servers_(ice_servers),
      settings_(settings),
      peer_name_(MakePeerName()),
      signaling_registration_(profile==opennow::gfn::cloud_session::ClientProfile::BrowserWebRtc) {
    // Explicit NVDEC is an experimental diagnostic mode. Always retain its
    // setup and first-frame evidence even when the general overlay is disabled.
    opennow::SetStreamDiagnosticsEnabled(
        settings_.debug_diagnostics || settings_.video_backend == "NVDEC");

    // Initialize libpeer
    peer_init();
    peer_connection_set_diagnostics_sink(&LibPeerDiagnosticSink);
    peer_connection_set_diagnostics_enabled(
        settings_.debug_diagnostics || settings_.video_backend == "NVDEC");
    renderer_ = std::make_unique<DKVideoRenderer>();
    audio_ = std::make_unique<AudioPipeline>();
    audio_->configure(settings_.audio_volume, opennow::streaming::AudioTargetMs(
        settings_.image_quality_mode, settings_.audio_buffer_ms));

    const bool previous_nvdec_crash = settings_.video_backend == "Auto" &&
                                      ConsumeNvdecCrashMarker();
    auto_safe_mode_used_ = previous_nvdec_crash;
    const bool force_software = settings_.video_backend == "Software" ||
                                previous_nvdec_crash;
    const bool require_hardware = settings_.video_backend == "NVDEC";
    decoder_ = std::make_unique<FFmpegVideoDecoder>();
    const int decoder_flags = force_software
        ? VIDEO_DECODER_FORCE_SOFTWARE
        : require_hardware
        ? VIDEO_DECODER_REQUIRE_HARDWARE
        : VIDEO_DECODER_PREFER_HARDWARE |
          VIDEO_DECODER_COPY_HARDWARE_FRAMES;
    decoder_setup_result_ = decoder_->setup(
        VIDEO_FORMAT_H264, settings_.width, settings_.height, settings_.fps, nullptr,
        decoder_flags);
    decoder_setup_diagnostics_ = decoder_->setup_diagnostics();

    // Auto remains resilient. Explicit NVDEC is strict so a hardware failure
    // is visible in the debug build instead of being mislabeled as NVDEC while
    // silently running the software decoder.
    if (decoder_setup_result_ != 0 && !force_software && !require_hardware) {
        nvdec_setup_diagnostics_ = decoder_setup_diagnostics_;
        decoder_->cleanup();
        decoder_ = std::make_unique<FFmpegVideoDecoder>();
        decoder_setup_result_ = decoder_->setup(
            VIDEO_FORMAT_H264, settings_.width, settings_.height, settings_.fps, nullptr,
            VIDEO_DECODER_FORCE_SOFTWARE);
        decoder_setup_diagnostics_ = decoder_->setup_diagnostics();
        decoder_fallback_used_ = decoder_setup_result_ == 0;
    }

    if (decoder_setup_result_ == 0 && decoder_->uses_hardware_decoder() &&
        decoder_->uses_hardware_frames())
        video_backend_name_ = "NVDEC-NVTEGRA/Deko3D-zero-copy(explicit)";
    else if (decoder_setup_result_ == 0 && decoder_->uses_hardware_decoder())
        video_backend_name_ = "NVDEC/NVTEGRA-copy/Deko3D-upload(auto-safe)";
    else if (decoder_setup_result_ == 0)
        video_backend_name_ = auto_safe_mode_used_
            ? "FFmpeg-SW-SLICE-3T/Deko3D-upload(crash-safe-mode)"
            : decoder_fallback_used_
            ? "FFmpeg-SW-SLICE-3T/Deko3D-upload(auto-fallback)"
            : "FFmpeg-SW-SLICE-3T/Deko3D-upload";
    else
        video_backend_name_ = require_hardware
            ? "NVDEC-required/setup-failed"
            : "decoder-setup-failed";

    if (decoder_setup_result_ == 0 && decoder_->uses_hardware_decoder())
        nvdec_marker_owned_ = CreateNvdecCrashMarker();
}

WebRtcSession::~WebRtcSession() {
    stop();
    peer_deinit();
}

void WebRtcSession::setup_peer_connection() {
    PeerConfiguration config = {};
    config.video_codec = CODEC_H264;
    config.audio_codec = CODEC_OPUS;
    config.datachannel = DATA_CHANNEL_STRING;
    config.onvideopacket = on_video_packet_cb;
    config.onaudiopacket = on_audio_packet_cb;
    config.onrtpsenderreport = on_rtp_sender_report_cb;
    config.user_data = this;

    if (ice_servers_.empty()) {
        ice_servers_.push_back({"stun:s1.stun.gamestream.nvidia.com:19308", "", ""});
        ice_servers_.push_back({"stun:stun.l.google.com:19302", "", ""});
        ice_servers_.push_back({"stun:stun1.l.google.com:19302", "", ""});
    }

    const size_t ice_count = std::min<size_t>(ice_servers_.size(), 5);
    for (size_t i = 0; i < ice_count; ++i) {
        config.ice_servers[i].urls = ice_servers_[i].url.c_str();
        config.ice_servers[i].username =
            ice_servers_[i].username.empty() ? nullptr : ice_servers_[i].username.c_str();
        config.ice_servers[i].credential =
            ice_servers_[i].credential.empty() ? nullptr : ice_servers_[i].credential.c_str();
    }

    pc_ = peer_connection_create(&config);
    if (!pc_) {
        brls::Logger::error("Failed to create PeerConnection");
        return;
    }

    peer_connection_onicecandidate(pc_, on_ice_candidate_cb);
    peer_connection_oniceconnectionstatechange(pc_, on_peer_state_change_cb);
    peer_connection_ondatachannel(pc_, on_datachannel_message_cb, on_datachannel_open_cb, on_datachannel_close_cb);
    // Note: libpeer requires patching or explicit callback for track data
    // Assuming on_track or similar is available. For now, we stub it.
    // peer_connection_ontrack(pc_, on_track_cb, this);
}

void WebRtcSession::start() {
    ingress_timing_ = {}; ingress_last_diagnostic_us_ = 0;
    last_input_ui_maintenance_us_ = 0;
    gamepad_send_window_max_us_ = heartbeat_send_window_max_us_ = 0;
    ui_maintenance_window_max_us_ = input_priority_ui_deferrals_ = 0;
    last_live_maintenance_us_ = 0;
    live_maintenance_window_max_us_ = live_maintenance_deferrals_ = input_idle_wait_skips_ = 0;
    (void)live_maintenance_performance_window_.Take();
    ingress_poll_gap_window_us_ = ingress_lock_wait_window_us_ = 0;
    ingress_raw_gap_window_us_ = ingress_assembly_window_us_ = ingress_callback_window_us_ = 0;
    stop_requested_.store(false, std::memory_order_release);
    session_started_at_ = std::chrono::steady_clock::now();
    diagnostic_attempt_id_ = ResetStreamTraceLog(session_id_);
    AppendStreamLog("SESSION start url=" + signaling_url_ +
                    " media=" + (media_ip_.empty() ? std::string("(auto)") : media_ip_) +
                    ":" + std::to_string(media_port_) +
                    " preset=" + settings_.label +
                    " " + std::to_string(settings_.width) + "x" +
                    std::to_string(settings_.height) + "@" +
                    std::to_string(settings_.fps) +
                    " bitrate=" + std::to_string(settings_.bitrate_kbps) +
                    " iceServers=" + std::to_string(ice_servers_.size()));
    AppendStreamLog("VIDEO backend=" + video_backend_name_ +
                    " requested=" + settings_.video_backend +
                    " fallback=" + std::to_string(decoder_fallback_used_ ? 1 : 0) +
                    " crashSafeMode=" + std::to_string(auto_safe_mode_used_ ? 1 : 0) +
                    " decoderSetup=" + std::to_string(decoder_setup_result_) +
                    " expectedFormat=" +
                    std::string(decoder_ && decoder_->uses_hardware_frames()
                        ? "NVTEGRA" : "YUV420P/NV12"));
    if (!nvdec_setup_diagnostics_.empty())
        AppendStreamLog("VIDEO nvdecAttempt=" + nvdec_setup_diagnostics_);
    AppendStreamLog("VIDEO decoderInit=" + decoder_setup_diagnostics_);
    const bool input_codec_ok = InputEncodingSelfTest();
    AppendStreamLog("INPUT codec_selftest ok=" + std::to_string(input_codec_ok ? 1 : 0));
    AppendInputLog("SESSION start codecSelfTest=" + std::to_string(input_codec_ok ? 1 : 0) +
                   " expectedController=xbox fixedReliableSid=0");
    AppendTraceLog("SESSION start");
    AppendTraceLog("url=" + signaling_url_);
    AppendTraceLog("sessionId=" + session_id_);
    AppendTraceLog("peerName=" + peer_name_);
    AppendTraceLog("mediaHint=" + (media_ip_.empty() ? std::string("(auto)") : media_ip_) +
                   ":" + std::to_string(media_port_));
    AppendTraceLog("preset=" + settings_.label + " resolution=" +
                   std::to_string(settings_.width) + "x" + std::to_string(settings_.height) +
                   " fps=" + std::to_string(settings_.fps) +
                   " bitrateKbps=" + std::to_string(settings_.bitrate_kbps));
    for (size_t i = 0; i < ice_servers_.size(); ++i) {
        AppendTraceLog("iceServer[" + std::to_string(i) + "] url=" + ice_servers_[i].url +
                       " user=" + (ice_servers_[i].username.empty() ? std::string("(empty)") : std::string("(set)")) +
                       " credential=" + (ice_servers_[i].credential.empty() ? std::string("(empty)") : std::string("(set)")));
    }
    setup_peer_connection();
    if (!pc_) {
        current_state_ = "PeerConnection setup failed";
        peer_terminal_kind_.store(
            static_cast<int>(opennow::PeerTerminalKind::Failed),
            std::memory_order_release);
        peer_terminal_.store(true, std::memory_order_release);
        AppendStreamLog("SESSION error peer_connection_setup_failed");
        return;
    }

    const bool browser = cloud_client_profile_==opennow::gfn::cloud_session::ClientProfile::BrowserWebRtc;
    const std::string sign_in_url = opennow::gfn::endpoints::SignInUrl(signaling_url_, peer_name_, session_id_, browser);
    AppendStreamLog("SIGNAL_DIAG route base=" + opennow::gfn::endpoints::SafeRoute(signaling_url_) +
        " final=" + opennow::gfn::endpoints::SafeRoute(sign_in_url) +
        " pairingPresent=" + std::to_string(!session_id_.empty()) +
        " pairingMatches=" + std::to_string(sign_in_url.ends_with("&pairing_id=" + session_id_)) +
        " peerRole=1 version=" + (browser ? std::string("3.0 registration=query") : std::string("2 registration=peer_info")));
    signaling_client_ = std::make_unique<SignalingClient>(sign_in_url);
    signaling_client_->set_on_message([this](const std::string& msg) {
        handle_signaling_message(msg);
    });

    std::vector<std::string> headers;
    headers.push_back("Origin: https://play.geforcenow.com");
    headers.push_back("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/131.0.0.0 Safari/537.36");
    if (!session_id_.empty()) {
        headers.push_back("Sec-WebSocket-Protocol: x-nv-sessionid." + session_id_);
    }
    signaling_client_->set_custom_headers(headers);

    if (!signaling_client_->connect()) {
        current_state_ = "WebSocket Connect Failed: " + signaling_client_->get_last_error();
        AppendStreamLog("SESSION error websocket_connect_failed " + signaling_client_->get_last_error());
        signaling_ready_ = false;
        peer_terminal_kind_.store(
            static_cast<int>(opennow::PeerTerminalKind::Failed),
            std::memory_order_release);
        peer_terminal_.store(true, std::memory_order_release);
        return;
    }
    signaling_ready_ = true;
    current_state_ = "Signaling connected, waiting for offer";
    AppendStreamLog("SIGNAL connected");
    send_peer_info();
    send_heartbeat();
    if (settings_.audio_enabled)
        audio_->start();
    start_decoder_worker();
    start_network_worker();
}


void WebRtcSession::start_network_worker() {
    if (network_running_.exchange(true))
        return;

    network_thread_ = std::thread(&WebRtcSession::network_loop, this);
    AppendStreamLog("TRANSPORT worker_started");
}

void WebRtcSession::network_loop() {
    ingress_last_diagnostic_us_ = NowUs();
    auto previous_cpu_us = WorkerCpuMicros();
    while (network_running_.load(std::memory_order_acquire)) {
        const auto input_generation = input_wake_generation_.load(std::memory_order_acquire);
        bool can_run = false;
        bool input_transport_ready = false;
        int batch_size = 0;
        const auto lock_requested_us = NowUs();
        {
            std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
            AtomicMax(ingress_lock_wait_window_us_, NowUs() - lock_requested_us);
            can_run = !stop_requested_.load(std::memory_order_acquire) &&
                      pc_ && signaling_ready_ && remote_description_set_ &&
                      (remote_ice_count_ > 0 || manual_candidate_added_);
            if (can_run) {
                const auto service_started_us = NowUs();
                uint64_t input_service_us = 0;
                unsigned input_reports_sent = 0;
                auto flush_priority_input = [&] {
                    if (opennow::input::EndInputBatch(input_reports_sent, input_service_us)) return;
                    const auto began = NowUs();
                    input_reports_sent += static_cast<unsigned>(flush_pending_gamepad_inputs_locked(4));
                    input_service_us += NowUs() - began;
                };
                flush_priority_input();
                // Recovery feedback must not depend on a UI frame obtaining the lock.
                if (peer_connection_get_state(pc_) == PEER_CONNECTION_COMPLETED &&
                    keyframe_needed_.exchange(false) && video_frame_gate_.Required())
                    request_keyframe("transport_recovery");
                const uint64_t sample_now = NowUs();
                if (sample_now - last_rtt_publish_us_ >= 200000) {
                    // Publish while the transport already owns the peer lock;
                    // UI try-lock contention must not leave RTT stale for seconds.
                    (void)get_network_rtt_stats(true);
                    (void)get_network_counters();
                    last_rtt_publish_us_ = sample_now;
                }
                if (sample_now - pause_sample_at_us_ >= 50000) {
                    pause_sample_at_us_ = sample_now;
                    PeerVideoRtpStats pause_rtp {};
                    if (peer_connection_get_video_rtp_stats(pc_, &pause_rtp) == 0 &&
                        frames_decoded_.load(std::memory_order_relaxed) > 120) {
                        const auto episode = live_pause_tracker_.Observe(
                            sample_now, last_video_packet_at_us_.load(std::memory_order_acquire),
                            last_audio_packet_at_us_.load(std::memory_order_acquire),
                            pause_rtp.packets_received, pause_rtp.access_units_dropped);
                        if (episode) {
                            playback_pause_count_.fetch_add(1);
                            playback_pause_us_.fetch_add(episode.duration_us);
                            if (episode.cause == opennow::streaming::PauseCause::Transport) {
                                transport_pause_count_.fetch_add(1);
                                transport_pause_us_.fetch_add(episode.duration_us);
                            } else if (episode.cause == opennow::streaming::PauseCause::VideoSource)
                                source_pause_count_.fetch_add(1);
                            else if (episode.cause == opennow::streaming::PauseCause::VideoAssembly)
                                assembly_pause_count_.fetch_add(1);
                            else
                                uncertain_pause_count_.fetch_add(1);
                            AppendStreamLog("PAUSE complete cause=" +
                                std::string(opennow::streaming::PauseCauseName(episode.cause)) +
                                " durationMs=" + std::to_string(episode.duration_us / 1000) +
                                " rttMs=" + std::to_string(get_network_rtt_ms()));
                        }
                    }
                }
                if (sample_now - adaptation_sample_at_us_ >= 500000) {
                    PeerVideoRtpStats rtp {};
                    peer_connection_get_video_rtp_stats(pc_, &rtp);
                    const uint64_t units = packets_received_.load();
                    const uint64_t decoded = frames_decoded_.load();
                    if (adaptation_sample_at_us_ != 0 && frames_decoded_.load() > 120) {
                        const float seconds = (sample_now - adaptation_sample_at_us_) / 1000000.0f;
                        size_t backlog;
                        { std::lock_guard<std::mutex> q(decoder_queue_mutex_); backlog = decoder_queue_.size(); }
                        adaptation_.Update({(units - adaptation_previous_units_) / seconds,
                            (decoded - adaptation_previous_decoded_) / seconds, settings_.fps,
                            rtp.access_units_dropped - adaptation_previous_damage_, backlog,
                            transport_pause_count_.load() - adaptation_previous_transport_pauses_,
                            playback_pause_count_.load() - adaptation_previous_playback_pauses_,
                            static_cast<unsigned>((sample_now - adaptation_sample_at_us_) / 1000),
                            arrival_jitter_us_.load(std::memory_order_relaxed)});
                        const int budget = adaptation_.WaitBudgetUs(settings_.image_quality_mode);
                        AVFrameHolder::instance().setWaitBudget(budget);
                        adaptation_wait_us_.store(budget);
                        const int reason = static_cast<int>(adaptation_.pressure);
                        const bool pressure_changed = adaptation_pressure_.exchange(reason) != reason;
                        if (pressure_changed || budget != previous_adaptation_wait_us_)
                            AppendStreamLog("ADAPT mode=" + settings_.image_quality_mode +
                                " reason=" + opennow::streaming::PressureName(adaptation_.pressure) +
                                " waitUs=" + std::to_string(budget) +
                                " protection=" + std::to_string(adaptation_.protection) +
                                " deliveryGuardUs=" + std::to_string(adaptation_.delivery_guard_us) +
                                " sampleMs=" + std::to_string(static_cast<unsigned>(seconds * 1000)) +
                                " serverBitrateControl=negotiation-only");
                        previous_adaptation_wait_us_ = budget;
                    }
                    adaptation_sample_at_us_ = sample_now;
                    adaptation_previous_units_ = units;
                    adaptation_previous_decoded_ = decoded;
                    adaptation_previous_damage_ = rtp.access_units_dropped;
                    adaptation_previous_transport_pauses_ = transport_pause_count_.load();
                    adaptation_previous_playback_pauses_ = playback_pause_count_.load();
                }
                // Send coalesced controller states on the transport thread.
                // This keeps the UI/render thread independent from a blocking
                // peer loop while still prioritising fresh input over a long
                // receive burst.
                flush_priority_input();
                // Motion-heavy frames arrive as short UDP bursts. Drain a
                // bounded batch while the peer lock is already held instead
                // of paying one mutex hand-off and scheduler yield per packet.
                constexpr int kMaximumDatagramsPerBatch = 24;
                constexpr auto kMaximumBatchTime = std::chrono::microseconds(1000);
                const auto batch_started_at = std::chrono::steady_clock::now();
                for (int packet = 0; packet < kMaximumDatagramsPerBatch; ++packet) {
                    // Never wait for a socket while holding the shared peer
                    // lock. Idle waiting below is interruptible by new input.
                    const uint64_t packet_begin = NowUs();
                    AtomicMax(ingress_poll_gap_window_us_, ingress_timing_.Poll(packet_begin));
                    const auto units_before_poll = packets_received_.load(std::memory_order_relaxed);
                    const int processed = peer_connection_loop_nonblocking(pc_);
                    if (processed > 0) {
                        PeerVideoRtpStats ingress_stats {};
                        if (peer_connection_get_video_rtp_stats(pc_, &ingress_stats) == 0) {
                            const auto now = NowUs();
                            if (ingress_stats.packets_received != ingress_timing_.raw_count)
                                AtomicMax(ingress_raw_gap_window_us_, opennow::streaming::IngressTiming::Age(now, ingress_timing_.last_raw_us));
                            ingress_timing_.Raw(ingress_stats.packets_received, now);
                            // If this poll completed an AU its last raw packet
                            // belongs to that AU, not the next one.
                            if (packets_received_.load(std::memory_order_relaxed) != units_before_poll)
                                ingress_timing_.observed_assembly_begin_us = 0;
                        }
                    }
                    const uint64_t packet_elapsed = NowUs() - packet_begin;
                    if (packet_elapsed >= 8000)
                        AppendStreamLog("TRANSPORT slow_call elapsedUs=" + std::to_string(packet_elapsed) +
                                        " live=" + std::to_string(peer_connection_get_state(pc_) == PEER_CONNECTION_COMPLETED));
                    if (processed == 0)
                        break;
                    batch_size++;
                    // Yield the peer lock after the current datagram when a
                    // fresh controller, keyboard or pointer report is waiting.
                    if (interactive_input_pending_.load(std::memory_order_acquire))
                        break;
                    // Enforce the time budget after every packet, including
                    // an expensive completed frame or retransmission callback.
                    if (std::chrono::steady_clock::now() - batch_started_at >=
                            kMaximumBatchTime) {
                        break;
                    }
                }
                const auto batch_us = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - batch_started_at).count());
                // A packet callback or socket call may outlive a controller
                // sample. Send it now instead of waiting for the next loop.
                // Preserve the initial handshake heartbeat before the first
                // report; only periodic keepalives yield to active input.
                if (last_input_heartbeat_sent_.time_since_epoch().count() == 0)
                    maybe_send_input_heartbeat();
                flush_priority_input();
                maybe_send_input_heartbeat();
                AtomicMax(transport_batch_us_max_, batch_us);
                transport_batch_window_.Observe(batch_us);
                const auto maintain_now=NowUs();
                if(opennow::streaming::LiveMaintenanceHandoff(peer_ever_completed_.load(),frames_decoded_.load()>0) &&
                    opennow::streaming::MaintenanceDue(maintain_now,last_live_maintenance_us_)) {
                    const bool urgent = !input_delivery_ready_.load(std::memory_order_acquire) ||
                        video_frame_gate_.Required() ||
                        peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED;
                    if (opennow::streaming::DeferLiveMaintenance(maintain_now, last_live_maintenance_us_,
                            interactive_input_pending_.load(std::memory_order_acquire), urgent)) {
                        live_maintenance_deferrals_.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        if(!last_live_maintenance_us_) AppendStreamLog("TRANSPORT live_signaling_owner=worker periodMs=100 inputDeferralMaxMs=5");
                        last_live_maintenance_us_=maintain_now;
                        try { poll(true); }
                        catch(...) {
                            AppendStreamLog("SESSION live_maintenance_failed");
                            peer_terminal_kind_.store(static_cast<int>(opennow::PeerTerminalKind::Failed));
                            peer_terminal_.store(true); request_stop();
                        }
                    }
                }
                input_transport_ready = !stop_requested_.load(std::memory_order_acquire) &&
                    input_ready_.load(std::memory_order_acquire) &&
                    peer_connection_get_state(pc_) == PEER_CONNECTION_COMPLETED;
                transport_service_window_.Observe(NowUs() - service_started_us);
                const auto diagnostic_now = NowUs();
                if (diagnostic_now - ingress_last_diagnostic_us_ >= 2000000 && packets_received_.load() > 0) {
                    PeerVideoRtpStats receive_stats {};
                    peer_connection_get_video_rtp_stats(pc_, &receive_stats);
                    const auto cpu_us = WorkerCpuMicros();
                    const auto cpu_delta = previous_cpu_us >= 0 && cpu_us >= previous_cpu_us ? cpu_us - previous_cpu_us : -1;
                    previous_cpu_us = cpu_us;
                    const auto ingress_window_ms = (diagnostic_now - ingress_last_diagnostic_us_) / 1000;
                    ingress_last_diagnostic_us_ = diagnostic_now;
                    AppendStreamLog("INGRESS window_ms=" + std::to_string(ingress_window_ms) + " pollGapMaxUs=" + std::to_string(ingress_poll_gap_window_us_.exchange(0)) +
                        " peerLockWaitMaxUs=" + std::to_string(ingress_lock_wait_window_us_.exchange(0)) +
                        " rawProgressGapMaxUs=" + std::to_string(ingress_raw_gap_window_us_.exchange(0)) +
                        " rawProgressAgeUs=" + std::to_string(opennow::streaming::IngressTiming::Age(diagnostic_now, ingress_timing_.last_raw_us)) +
                        " unitAgeUs=" + std::to_string(opennow::streaming::IngressTiming::Age(diagnostic_now, ingress_timing_.last_au_us)) +
                        " observedAssemblySpanMaxUs=" + std::to_string(ingress_assembly_window_us_.exchange(0)) +
                        " callbackMaxUs=" + std::to_string(ingress_callback_window_us_.exchange(0)) +
                        " gamepadSendMaxUs=" + std::to_string(gamepad_send_window_max_us_.exchange(0)) +
                        " heartbeatSendMaxUs=" + std::to_string(heartbeat_send_window_max_us_.exchange(0)) +
                        " uiMaintenanceMaxUs=" + std::to_string(ui_maintenance_window_max_us_.exchange(0)) +
                        " liveMaintenanceMaxUs=" + std::to_string(live_maintenance_window_max_us_.exchange(0)) +
                        " liveMaintenanceDeferralsTotal=" + std::to_string(live_maintenance_deferrals_.load()) +
                        " inputIdleWaitSkipsTotal=" + std::to_string(input_idle_wait_skips_.load()) +
                        " inputPriorityUiDeferrals=" + std::to_string(input_priority_ui_deferrals_.exchange(0)) +
                        " workerCpuUs=" + std::to_string(cpu_delta) +
                        " rxBatchActive=" + std::to_string(receive_stats.receive_batch_active) +
                        " rxBatchCalls=" + std::to_string(receive_stats.receive_batch_calls) +
                        " rxBatchDatagrams=" + std::to_string(receive_stats.receive_batch_datagrams) +
                        " rxBatchHits=" + std::to_string(receive_stats.receive_batch_hits) +
                        " rxBatchRejected=" + std::to_string(receive_stats.receive_batch_rejected) +
                        " timing=application_observation_not_kernel_arrival");
                }
                if (batch_size > 0) {
                    transport_batches_.fetch_add(1, std::memory_order_relaxed);
                    transport_datagrams_.fetch_add(
                        static_cast<uint64_t>(batch_size), std::memory_order_relaxed);
                    AtomicMax(transport_batch_high_water_, static_cast<size_t>(batch_size));
                }
            }
        }

        const int backoff_ms =
            opennow::network::LoopBackoffMilliseconds(can_run, batch_size);
        if (backoff_ms > 0) {
            std::unique_lock<std::mutex> wake_lock(outbound_input_mutex_);
            // Test the actual queue under its mutex: old queued work need not
            // change input_wake_generation_ during this transport pass.
            const auto queued_input_can_run = [&] {
                const bool queued = std::any_of(pending_gamepad_reports_.begin(),
                    pending_gamepad_reports_.end(), [](const auto& reports) { return !reports.empty(); });
                return opennow::network::QueuedInputCanRun(input_transport_ready,
                    input_delivery_ready_.load(std::memory_order_acquire), queued,
                    NowUs(), input_backpressure_until_us_);
            };
            if (queued_input_can_run())
                input_idle_wait_skips_.fetch_add(1, std::memory_order_relaxed);
            else
                network_wake_cv_.wait_for(wake_lock, std::chrono::milliseconds(backoff_ms), [&] {
                    return !network_running_.load(std::memory_order_acquire) ||
                        input_wake_generation_.load(std::memory_order_acquire) != input_generation ||
                        queued_input_can_run();
                });
        } else
            std::this_thread::yield();
    }

    AppendStreamLog("TRANSPORT worker_stopped");
}


void WebRtcSession::poll(bool transport_owner) {
    if (stop_requested_.load(std::memory_order_acquire))
        return;

    if (!peer_ever_completed_.load(std::memory_order_acquire) &&
        session_started_at_.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() - session_started_at_ >=
            std::chrono::seconds(30)) {
        current_state_ = offer_received_.load() ? "ICE connection timed out" : "Signaling offer timed out";
        startup_timed_out_.store(true, std::memory_order_release);
        peer_terminal_kind_.store(
            static_cast<int>(opennow::PeerTerminalKind::Failed),
            std::memory_order_release);
        peer_terminal_.store(true, std::memory_order_release);
        const auto end_reason=offer_received_.load() ? opennow::signaling::EndReason::InitialIceTimeout :
            opennow::signaling::EndReason::InitialOfferTimeout;
        signaling_end_reason_.store(end_reason);
        AppendStreamLog("SIGNAL_DIAG end origin=startup reason=" +
            std::string(opennow::signaling::EndReasonName(end_reason)));
        AppendStreamLog("SESSION error startup_timeout seconds=30 phase=" +
            std::string(offer_received_.load() ? "ice" : "waiting_offer"));
        request_stop();
        return;
    }

    const bool live_handoff=opennow::streaming::LiveMaintenanceHandoff(
        peer_ever_completed_.load(std::memory_order_acquire),
        frames_decoded_.load(std::memory_order_relaxed)>0);
    if (live_handoff && !transport_owner) {
        // Renderer statistics remain on their owning UI thread. Read-only
        // diagnostics may be skipped; no signaling/socket IO runs in this path.
        std::unique_lock<std::recursive_mutex> live_lock(peer_mutex_,std::try_to_lock);
        if(live_lock.owns_lock()) maybe_log_stream_diagnostics();
        return;
    }
    const auto maintenance_requested_us = NowUs();
    if (opennow::input::DeferUiMaintenance(input_delivery_ready_.load(std::memory_order_acquire),
            interactive_input_pending_.load(std::memory_order_acquire), maintenance_requested_us,
            last_input_ui_maintenance_us_)) {
        input_priority_ui_deferrals_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Rendering must never wait behind a UDP receive burst. All state updates
    // below are periodic maintenance and can safely run on the next frame.
    std::unique_lock<std::recursive_mutex> lock(peer_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        ui_poll_lock_skips_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    last_input_ui_maintenance_us_ = NowUs();
    struct MaintenanceTimer {
        std::atomic<uint64_t>& maximum;
        opennow::performance::IntervalMaximum* performance_window;
        uint64_t began_us;
        ~MaintenanceTimer() {
            const auto elapsed = NowUs() - began_us;
            AtomicMax(maximum, elapsed);
            if (performance_window) performance_window->Observe(elapsed);
        }
    } maintenance_timer {transport_owner ? live_maintenance_window_max_us_ : ui_maintenance_window_max_us_,
        transport_owner ? &live_maintenance_performance_window_ : nullptr, last_input_ui_maintenance_us_};
    if (signaling_client_) {
        signaling_client_->poll();
    }

    maybe_send_keepalive();

    if (pc_ && signaling_ready_) {
        if (!remote_description_set_) {
            current_state_ = "Signaling connected, waiting for offer";
            if(!transport_owner) maybe_log_stream_diagnostics();
            return;
        }

        maybe_add_manual_media_candidate();

        if (remote_ice_count_ == 0 && !manual_candidate_added_) {
            current_state_ = "Waiting for remote ICE";
            if(!transport_owner) maybe_log_stream_diagnostics();
            return;
        }

        const PeerConnectionState state = peer_connection_get_state(pc_);
        current_state_ = std::string("Peer ") + peer_connection_state_to_string(state);
        if (state == PEER_CONNECTION_COMPLETED) {
            if (keyframe_needed_.exchange(false) && video_frame_gate_.Required())
                request_keyframe("decoder_resync");
            maybe_open_datachannel();
            maybe_activate_input();
            maybe_send_startup_control_messages();
            // Input heartbeat belongs to the network worker, never the UI.
            maybe_recover_rtp_damage();
            maybe_recover_decode_stall();
        }
        if (state == PEER_CONNECTION_COMPLETED && !initial_keyframe_requested_) {
            last_startup_keyframe_request_ = std::chrono::steady_clock::now();
            request_keyframe("stream_start");
            initial_keyframe_requested_ = true;
        }
        maybe_request_startup_keyframe_retry();
        if(!transport_owner) maybe_log_stream_diagnostics();
    }
}


void WebRtcSession::note_recovery_end(bool deadline) {
    const auto reason=deadline ? opennow::signaling::EndReason::RecoveryDeadline :
        opennow::signaling::EndReason::RecoveryPeerTerminal;
    auto unset=opennow::signaling::EndReason::None;
    signaling_end_reason_.compare_exchange_strong(unset,reason);
    AppendStreamLog("SIGNAL_DIAG end origin=recovery reason=" +
        std::string(opennow::signaling::EndReasonName(reason)) +
        " offer=" + std::to_string(offer_received_.load() ? 1 : 0) +
        " video=" + std::to_string(frames_decoded_.load()>0 ? 1 : 0));
}

void WebRtcSession::request_stop() {
    const bool first_request =
        !stop_requested_.exchange(true, std::memory_order_acq_rel);

    network_running_.store(false, std::memory_order_release);
    input_delivery_ready_.store(false, std::memory_order_release);
    clear_pending_gamepad_inputs();
    network_wake_cv_.notify_all();
    decoder_running_.store(false, std::memory_order_release);
    decoder_queue_cv_.notify_all();
    if (first_request) {
        auto unset=opennow::signaling::EndReason::None;
        signaling_end_reason_.compare_exchange_strong(unset,opennow::signaling::EndReason::StopRequested);
        AppendStreamLog("SIGNAL_DIAG stop reason=" +
            std::string(opennow::signaling::EndReasonName(signaling_end_reason_.load())) +
            " offer=" + std::to_string(offer_received_.load() ? 1 : 0) +
            " video=" + std::to_string(frames_decoded_.load()>0 ? 1 : 0));
        AppendInputLog("SESSION asynchronous_stop_requested");
    }
}

void WebRtcSession::stop() {
    request_stop();
    if (network_thread_.joinable())
        network_thread_.join();

    if (audio_)
        audio_->stop();

    if (decoder_thread_.joinable())
        decoder_thread_.join();

    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        clear_decoder_queue_locked();
        decoder_buffer_pool_.clear();
    }

    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    if (!pc_ && !decoder_)
        return;

    log_stream_summary("stop");
    if (opennow::StreamDiagnosticsEnabled()) {
        const std::string phase = frames_decoded_.load() > 0 ? "video" :
            !offer_received_.load() ? "waiting_offer" : !peer_ever_completed_.load() ? "ice_dtls" : "first_picture";
        AppendStreamLog("ATTEMPT end id=" + std::to_string(diagnostic_attempt_id_) + " phase=" + phase +
            " startup_timeout=" + std::to_string(startup_timed_out_.load()) + " frames=" + std::to_string(frames_decoded_.load()));
    }
    AppendInputLog("SUMMARY attempts=" + std::to_string(gamepad_input_attempt_count_) +
                   " blocked=" + std::to_string(gamepad_input_blocked_count_) +
                   " reportsSent=" + std::to_string(gamepad_tx_count_) +
                   " sendFailures=" + std::to_string(gamepad_send_failure_count_) +
                   " mouseSent=" + std::to_string(mouse_tx_count_) +
                   " sctpOpen=" + std::to_string(datachannel_opened_ ? 1 : 0) +
                   " channelRequested=" + std::to_string(datachannel_open_requested_ ? 1 : 0) +
                   " inputReady=" + std::to_string(input_ready_ ? 1 : 0) +
                   " protocol=" + std::to_string(input_protocol_version_));
    FlushDiagnosticLogs();
    if (pc_) {
        peer_connection_close(pc_);
        peer_connection_destroy(pc_);
        pc_ = nullptr;
    }
    // Zero-copy Deko3D mappings reference NVDEC-owned memory, so release all
    // renderer mappings before destroying the decoder hardware frame pool.
    if (renderer_)
        renderer_.reset();
    AVFrameHolder::instance().cleanup();
    if (decoder_) {
        decoder_->cleanup();
        decoder_.reset();
    }
    if (nvdec_marker_owned_) {
        std::remove(kNvdecActiveMarker);
        nvdec_marker_owned_ = false;
    }
    FlushDiagnosticLogs();
}
