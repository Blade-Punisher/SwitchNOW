#include "webrtc_session.hpp"
#include <cstring>
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "stream_diagnostics.hpp"
#include "IceDiagnostics.hpp"
#include "internal.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

using namespace opennow::webrtc::internal;


namespace
{
std::atomic<uint64_t> current_attempt_id {0};

std::chrono::steady_clock::time_point& InputLogStartTime()
{
    static auto start = std::chrono::steady_clock::now();
    return start;
}

struct DiagnosticLogFiles
{
    std::ofstream signaling;
    std::ofstream trace;
    std::ofstream input;
    std::ofstream audio;
    std::ofstream attempts;

    void close()
    {
        signaling.close();
        trace.close();
        input.close();
        audio.close();
        attempts.close();
    }

    void ensure_open()
    {
        if (!signaling.is_open())
            signaling.open("sdmc:/switch/SwitchNOW/signaling.log", std::ios::app);
        if (!trace.is_open())
            trace.open("sdmc:/switch/SwitchNOW/stream_trace.log", std::ios::app);
        if (!input.is_open())
            input.open("sdmc:/switch/SwitchNOW/input.log", std::ios::app);
        if (!audio.is_open())
            audio.open("sdmc:/switch/SwitchNOW/audio.log", std::ios::app);
        if (!attempts.is_open())
            attempts.open("sdmc:/switch/SwitchNOW/stream_attempts.log", std::ios::app);
    }

    void flush()
    {
        if (signaling.is_open())
            signaling.flush();
        if (trace.is_open())
            trace.flush();
        if (input.is_open())
            input.flush();
        if (audio.is_open())
            audio.flush();
        if (attempts.is_open()) attempts.flush();
    }
};

// Only this worker touches the app's SD files. Producers hold the queue lock
// briefly; an SD stall can discard diagnostics but cannot stall RTP or input.
class DiagnosticWriter {
public:
    DiagnosticWriter() : worker_([this] { run(); }) {}
    ~DiagnosticWriter() {
        flush();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }

    void append(unsigned channels, std::string line) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queued_bytes_ + line.size() > 256 * 1024 || entries_.size() >= 2048) {
            ++discarded_;
            ++total_discarded_;
            return;
        }
        queued_bytes_ += line.size();
        entries_.push_back({channels, std::move(line)});
        wake_.notify_one();
    }

    void request_flush() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            flush_requested_ = true;
        }
        wake_.notify_one();
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto target = ++requested_flush_;
        flush_requested_ = true;
        wake_.notify_one();
        done_.wait(lock, [&] { return completed_flush_ >= target; });
    }

    void reset() {
        std::unique_lock<std::mutex> lock(mutex_);
        const auto target = ++requested_reset_;
        wake_.notify_one();
        done_.wait(lock, [&] { return completed_reset_ >= target; });
    }

    uint64_t discarded() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_discarded_;
    }

    std::vector<opennow::diagnostics::FileSnapshot> snapshot() {
        auto job = std::make_shared<SnapshotPromise>();
        auto result = job->get_future();
        { std::lock_guard<std::mutex> lock(mutex_); snapshots_.push_back(job); }
        wake_.notify_one();
        return result.get();
    }

private:
    using SnapshotPromise = std::promise<std::vector<opennow::diagnostics::FileSnapshot>>;
    std::vector<std::shared_ptr<SnapshotPromise>> snapshots_;
    struct Entry { unsigned channels; std::string line; };
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    std::deque<Entry> entries_;
    std::size_t queued_bytes_ = 0;
    uint64_t discarded_ = 0;
    uint64_t total_discarded_ = 0;
    uint64_t requested_flush_ = 0, completed_flush_ = 0;
    uint64_t requested_reset_ = 0, completed_reset_ = 0;
    bool flush_requested_ = false;
    bool stopping_ = false;
    DiagnosticLogFiles files_;
    std::thread worker_;

    void write(const Entry& entry) {
        files_.ensure_open();
        if ((entry.channels & 1) && files_.signaling.is_open())
            files_.signaling << entry.line;
        if ((entry.channels & 2) && files_.trace.is_open())
            files_.trace << entry.line;
        if ((entry.channels & 4) && files_.input.is_open())
            files_.input << entry.line;
        if ((entry.channels & 8) && files_.audio.is_open())
            files_.audio << entry.line;
        if ((entry.channels & 16) && files_.attempts.is_open()) files_.attempts << entry.line;
    }

    void reset_files() {
        files_.close();
        opennow::diagnostics::ArchiveAttempts("sdmc:/switch/SwitchNOW",
            {"signaling.log", "stream_trace.log", "input.log", "audio.log"});
        const struct Header { const char* path; const char* text; } headers[] = {
            {"sdmc:/switch/SwitchNOW/signaling.log", "SwitchNOW signaling and media log\nOne file per stream attempt.\n======================================\n"},
            {"sdmc:/switch/SwitchNOW/stream_trace.log", "SwitchNOW stream trace\nOne file per stream attempt. Safe to send for debugging.\n=======================================================\n"},
            {"sdmc:/switch/SwitchNOW/input.log", "SwitchNOW input flight recorder\nController samples -> Xbox encoding -> DataChannel -> SCTP result.\n===============================================================\n"},
            {"sdmc:/switch/SwitchNOW/audio.log", ""},
        };
        for (const auto& header : headers) {
            std::ofstream file(header.path, std::ios::trunc);
            if (file.is_open()) file << header.text;
        }
        files_.ensure_open();
    }

    void run() {
#ifdef __SWITCH__
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/SwitchNOW", 0777);
#endif
        for (;;) {
            std::deque<Entry> batch;
            std::vector<std::shared_ptr<SnapshotPromise>> snapshots;
            bool flush = false, reset = false, stop = false;
            uint64_t flush_target = 0, reset_target = 0, lost = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || !entries_.empty() ||
                    flush_requested_ || requested_reset_ != completed_reset_ || !snapshots_.empty(); });
                snapshots.swap(snapshots_);
                batch.swap(entries_);
                queued_bytes_ = 0;
                flush = flush_requested_ || stopping_;
                flush_requested_ = false;
                reset = requested_reset_ != completed_reset_;
                flush_target = requested_flush_;
                reset_target = requested_reset_;
                lost = discarded_;
                discarded_ = 0;
                stop = stopping_;
            }
            for (const auto& entry : batch) write(entry);
            if (lost) write({3, "APP DIAGNOSTICS queued_entries_discarded=" +
                std::to_string(lost) + "\n"});
            if (flush || reset) files_.flush();
            for (const auto& job : snapshots) {
                try {
                    job->set_value(opennow::diagnostics::CaptureClosedFiles(
                        "sdmc:/switch/SwitchNOW", {"signaling.log", "stream_trace.log", "input.log", "audio.log", "stream_attempts.log",
                            "attempt1_signaling.log", "attempt1_stream_trace.log", "attempt1_input.log", "attempt1_audio.log",
                            "attempt2_signaling.log", "attempt2_stream_trace.log", "attempt2_input.log", "attempt2_audio.log"},
                        [this] { files_.flush(); files_.close(); }));
                } catch (...) { job->set_exception(std::current_exception()); }
            }
            if (reset) reset_files();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (flush) completed_flush_ = flush_target;
                if (reset) completed_reset_ = reset_target;
                done_.notify_all();
                if (stop && entries_.empty()) break;
            }
        }
        files_.close();
    }
};

DiagnosticWriter& Writer() {
    static DiagnosticWriter writer;
    return writer;
}

} // namespace

void opennow::AppendAudioDiagnostic(std::string line) {
    if (StreamDiagnosticsEnabled()) Writer().append(8, std::move(line));
}

std::vector<opennow::diagnostics::FileSnapshot> opennow::CaptureStreamDiagnosticFiles() {
    return Writer().snapshot();
}

namespace opennow::webrtc::internal
{

void AppendInputLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled()) return;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - InputLogStartTime()).count();
    Writer().append(4, "[attempt=" + std::to_string(current_attempt_id.load()) + "][+" + std::to_string(elapsed_ms) + "ms] " + line + "\n");
}

void AppendStreamLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled()) return;
    const auto log_start = InputLogStartTime();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - log_start).count();
    const auto stamp = "[attempt=" + std::to_string(current_attempt_id.load()) + "][+" + std::to_string(elapsed_ms) + "ms] ";
    // Retain separate trace and signaling prefixes in one queued record.
    Writer().append(1, stamp + line + "\n");
    Writer().append(2, stamp + "APP " + line + "\n");
    if (line.starts_with("ATTEMPT ")) Writer().append(16, stamp + line + "\n");
}

void AppendLibPeerLog(const char* line, bool input_relevant)
{
    if (!opennow::StreamDiagnosticsEnabled() || !line) return;
    const auto log_start = InputLogStartTime();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - log_start).count();
    Writer().append(input_relevant ? 7 : 3,
                    "[attempt=" + std::to_string(current_attempt_id.load()) + "][+" + std::to_string(elapsed_ms) + "ms] " + line + "\n");
}

uint64_t ResetStreamTraceLog(const std::string& session_id)
{
    const auto previous = current_attempt_id.load();
    const auto id = std::max(previous + 1, NowUs());
    current_attempt_id.store(id);
    if (!opennow::StreamDiagnosticsEnabled()) return id;
    // Called before the transport starts. A synchronous reset ensures a new
    // session never mixes records from the previous one.
    Writer().reset();
    InputLogStartTime() = std::chrono::steady_clock::now();
    Writer().append(31, "ATTEMPT start id=" + std::to_string(id) + " cloud_session=" + session_id + "\n");
    return id;
}

void RequestDiagnosticFlush()
{
    if (opennow::StreamDiagnosticsEnabled()) Writer().request_flush();
}

void FlushDiagnosticLogs()
{
    if (!opennow::StreamDiagnosticsEnabled()) return;
    Writer().flush();
    peer_connection_flush_diagnostics();
}

void AppendTraceLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled()) return;
    static const auto trace_start = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - trace_start).count();
    Writer().append(2, "[+" + std::to_string(elapsed_ms) + "ms] " + line + "\n");
}

void AppendTraceBlock(const std::string& title, const std::string& body)
{
    if (!opennow::StreamDiagnosticsEnabled()) return;
    AppendTraceLog("----- " + title + " BEGIN bytes=" + std::to_string(body.size()) + " -----");
    Writer().append(2, body + (body.empty() || body.back() == '\n' ? "" : "\n"));
    AppendTraceLog("----- " + title + " END -----");
}

std::string PreviewText(const std::string& value, size_t max_chars)
{
    if (value.size() <= max_chars)
        return value;
    return value.substr(0, max_chars) + "...";
}

} // namespace opennow::webrtc::internal

void WebRtcSession::record_ui_event(const std::string& event) {
    AppendStreamLog("UI " + event);
}


void WebRtcSession::maybe_log_stream_diagnostics() {
    if (!settings_.debug_diagnostics)
        return;
    if (!pc_)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_stream_diagnostic_log_.time_since_epoch().count() != 0 &&
        now - last_stream_diagnostic_log_ < std::chrono::seconds(2)) {
        return;
    }

    const PeerConnectionState state = peer_connection_get_state(pc_);
    if (state == PEER_CONNECTION_FAILED ||
        state == PEER_CONNECTION_CLOSED ||
        state == PEER_CONNECTION_DISCONNECTED) {
        return;
    }
    if (offer_received_.load() && state != PEER_CONNECTION_CHECKING &&
        state != PEER_CONNECTION_CONNECTED &&
        state != PEER_CONNECTION_COMPLETED &&
        packets_received_.load() == last_logged_packet_count_ &&
        frames_decoded_.load() == last_logged_frame_count_.load(std::memory_order_relaxed) &&
        decode_errors_.load() == last_logged_decode_error_count_.load(std::memory_order_relaxed)) {
        return;
    }

    last_stream_diagnostic_log_ = now;
    log_stream_summary("periodic");
}


void WebRtcSession::log_stream_summary(const char* reason) {
    const auto now = std::chrono::steady_clock::now();
    long long since_start_ms = -1;
    long long since_completed_ms = -1;

    if (session_started_at_.time_since_epoch().count() != 0) {
        since_start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - session_started_at_).count();
    }
    if (peer_completed_at_.time_since_epoch().count() != 0) {
        since_completed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - peer_completed_at_).count();
    }

    const VideoPerformanceCounters video = get_video_performance();
    double metric_seconds = 0.0;
    if (last_video_metric_snapshot_.time_since_epoch().count() != 0) {
        metric_seconds = std::chrono::duration<double>(now - last_video_metric_snapshot_).count();
    }
    const auto rate = [metric_seconds](uint64_t current, uint64_t previous) {
        return metric_seconds > 0.0 && current >= previous
            ? static_cast<double>(current - previous) / metric_seconds
            : 0.0;
    };
    const double incoming_fps = rate(video.access_units, last_logged_video_performance_.access_units);
    const double decoded_fps = rate(video.decoded_frames, last_logged_video_performance_.decoded_frames);
    const double presented_fps = rate(video.presented_frames, last_logged_video_performance_.presented_frames);
    const double bitrate_mbps = rate(video.access_unit_bytes, last_logged_video_performance_.access_unit_bytes) * 8.0 / 1000000.0;
    last_video_metric_snapshot_ = now;
    last_logged_video_performance_ = video;

    int pair_total = 0;
    int pair_frozen = 0;
    int pair_checking = 0;
    int pair_succeeded = 0;
    int pair_failed = 0;
    PeerVideoRtpStats rtp_stats {};
    if (pc_) {
        peer_connection_get_ice_candidate_pair_stats(
            pc_, &pair_total, &pair_frozen, &pair_checking, &pair_succeeded, &pair_failed);
        (void)peer_connection_get_video_rtp_stats(pc_, &rtp_stats);
    }
    PeerNetworkRttStats network_rtt {};
    network_rtt.current_ms = -1;
    network_rtt.stun_current_ms = -1;
    network_rtt.sctp_current_ms = -1;
    network_rtt.min_ms = -1;
    network_rtt.median_ms = -1;
    network_rtt.p95_ms = -1;
    network_rtt.jitter_ms = -1;
    if (pc_)
        (void)peer_connection_get_rtt_stats(pc_, &network_rtt);
    const char* rtt_source =
        network_rtt.source == PEER_RTT_SOURCE_STUN ? "stun" :
        network_rtt.source == PEER_RTT_SOURCE_SCTP ? "sctp" : "none";
    const int64_t rtt_sample_age_ms =
        network_rtt.samples > 0 ? static_cast<int64_t>(network_rtt.stun_sample_age_ms) : -1;
    const auto& frame_holder = AVFrameHolder::instance();
    const auto frame_headers = frame_holder.getHeaderPoolStats();
    const auto ready_timing = frame_holder.getReadyMetrics();
    auto render_backend_stats=last_reported_render_stats_;
    {
        std::unique_lock<std::mutex> snapshot_lock(render_snapshot_mutex_,std::try_to_lock);
        if(snapshot_lock.owns_lock()) {
            render_backend_stats=render_stats_snapshot_;
            last_reported_render_stats_=render_backend_stats;
        }
    }

    // Structured summaries preserve startup state without raw protocol data.
    // No extra clock, socket query or JSON parsing is needed here.
    if(frames_decoded_.load()==0 || !reason || std::strcmp(reason,"periodic")!=0)
        AppendStreamLog(signaling_diagnostics_.Summary(signaling_client_ && signaling_client_->is_connected(),
        offer_received_.load(),static_cast<unsigned>(std::max(0,remote_ice_count_)),
        frames_decoded_.load()>0,signaling_end_reason_.load(),
        signaling_client_ ? signaling_client_->get_last_error() : std::string{}) +
        " registrationConfirmed=" + std::to_string(signaling_registration_.Confirmed() ? 1 : 0) +
        " registrationAttemptLimit=" + std::to_string(opennow::signaling::RegistrationPolicy::MaximumAttempts));
    if (signaling_client_ && (frames_decoded_.load()==0 || !reason || std::strcmp(reason,"periodic")!=0))
        AppendStreamLog("SIGNAL_DIAG websocket " + signaling_client_->get_receive_diagnostics());
    AppendStreamLog("STREAM diag reason=" + std::string(reason ? reason : "unknown") +
                    " state=" + current_state_ +
                    " sinceStartMs=" + std::to_string(since_start_ms) +
                    " sinceCompletedMs=" + std::to_string(since_completed_ms) +
                    " signalingRx=" + std::to_string(signaling_rx_count_) +
                    " offers=" + std::to_string(offer_count_) +
                    " iceRemote=" + std::to_string(remote_ice_count_) +
                    " iceLocal=" + std::to_string(local_ice_count_) +
                    " icePairs=" + std::to_string(pair_total) +
                    "/" + std::to_string(pair_frozen) +
                    "/" + std::to_string(pair_checking) +
                    "/" + std::to_string(pair_succeeded) +
                    "/" + std::to_string(pair_failed) +
                    " rttMs=" + std::to_string(network_rtt.current_ms) +
                    " rttSource=" + rtt_source +
                    " rttStunMs=" + std::to_string(network_rtt.stun_current_ms) +
                    " rttSctpMs=" + std::to_string(network_rtt.sctp_current_ms) +
                    " rttMinMs=" + std::to_string(network_rtt.min_ms) +
                    " rttMedianMs=" + std::to_string(network_rtt.median_ms) +
                    " rttP95Ms=" + std::to_string(network_rtt.p95_ms) +
                    " rttJitterMs=" + std::to_string(network_rtt.jitter_ms) +
                    " rttSampleAgeMs=" + std::to_string(rtt_sample_age_ms) +
                    " rttSamples=" + std::to_string(network_rtt.samples) +
                    " stunProbe=" + std::to_string(network_rtt.probes_sent) +
                    "/" + std::to_string(network_rtt.probes_matched) +
                    "/" + std::to_string(network_rtt.probes_lost) +
                    "/" + std::to_string(network_rtt.probes_unmatched) +
                    "/" + std::to_string(network_rtt.probe_in_flight) +
                    " hbRx=" + std::to_string(heartbeat_rx_count_) +
                    " hbTx=" + std::to_string(heartbeat_tx_count_) +
                    " dcOpen=" + std::to_string(datachannel_opened_ ? 1 : 0) +
                    " dcReq=" + std::to_string(datachannel_open_requested_ ? 1 : 0) +
                    " dcAttempts=" + std::to_string(datachannel_open_attempts_) +
                    " dcStartup=" + std::to_string(startup_control_sent_ ? 1 : 0) +
                    " rel=" + std::to_string(reliable_input_channel_requested_ ? 1 : 0) +
                    " pr=" + std::to_string(partial_input_channel_requested_ ? 1 : 0) +
                    " inputReady=" + std::to_string(input_ready_ ? 1 : 0) +
                    " inputProto=" + std::to_string(input_protocol_version_) +
                    " inputHbTx=" + std::to_string(input_heartbeat_tx_count_) +
                    " packets=" + std::to_string(packets_received_.load()) +
                    " frames=" + std::to_string(frames_decoded_.load()) +
                    " decodeErrors=" + std::to_string(decode_errors_.load()) +
                    " keyframes=" + std::to_string(keyframe_request_attempts_) +
                    " videoBackend=" + video_backend_name_ +
                    " inputMaxUs uiBlock/send/interval=" +
                    std::to_string(input_lock_us_max_.load()) + "/" +
                    std::to_string(input_send_us_max_.load()) + "/" +
                    std::to_string(input_interval_us_max_.load()) +
                    " inputQueue waitMaxUs/coalesced=" +
                    std::to_string(input_queue_wait_us_max_.load()) + "/" +
                    std::to_string(gamepad_reports_coalesced_.load()) +
                    " inputEdgesDropped=" +
                    std::to_string(gamepad_button_edges_dropped_.load()) +
                    " inputBackpressure=" + std::to_string(input_backpressure_count_.load()) +
                    " mode=" + settings_.image_quality_mode +
                    " adaptation=" + opennow::streaming::PressureName(
                        static_cast<opennow::streaming::Pressure>(adaptation_pressure_.load())) +
                    " adaptiveWaitUs=" + std::to_string(adaptation_wait_us_.load()) +
                    " livePause transport/countMs=" +
                    std::to_string(transport_pause_count_.load()) + "/" +
                    std::to_string(transport_pause_us_.load() / 1000) +
                    " source/assembly=" + std::to_string(source_pause_count_.load()) + "/" +
                    std::to_string(assembly_pause_count_.load()) +
                    " playbackPause/countMs/uncertain=" +
                    std::to_string(playback_pause_count_.load()) + "/" +
                    std::to_string(playback_pause_us_.load() / 1000) + "/" +
                    std::to_string(uncertain_pause_count_.load()) +
                    " recoveryActiveMs=" + std::to_string(recovery_started_us_.load() ?
                        (NowUs() - recovery_started_us_.load()) / 1000 : 0) +
                    " decodeQueue=" + std::to_string(video.decode_queue_size) +
                    " recoveryPresentMaxMs=" + std::to_string(recovery_present_max_us_.load() / 1000) +
                    " inputSctpUsMax=" + std::to_string(input_sctp_us_max_.load()) +
                    " inputP95P99Us queue=" +
                    std::to_string(LatencyPercentile95(input_queue_wait_buckets_)) +
                    "/" + std::to_string(LatencyPercentile99(input_queue_wait_buckets_)) +
                    " send=" +
                    std::to_string(LatencyPercentile95(input_send_buckets_)) +
                    "/" + std::to_string(LatencyPercentile99(input_send_buckets_)) +
                    " recovery count/avgMs/maxMs=" +
                    std::to_string(recovery_duration_count_.load()) + "/" +
                    std::to_string(recovery_duration_count_.load() > 0
                        ? recovery_duration_us_total_.load() /
                          recovery_duration_count_.load() / 1000 : 0) + "/" +
                    std::to_string(recovery_duration_us_max_.load() / 1000) +
                    " transportBatchUsMax=" + std::to_string(transport_batch_us_max_.load()) +
                    " diagnosticQueueDiscarded=" + std::to_string(Writer().discarded()) +
                    " uiPollLockSkips=" +
                    std::to_string(ui_poll_lock_skips_.load()) +
                    " videoClockLockSkips=" +
                    std::to_string(video_clock_lock_skips_.load()) +
                    " videoClockReason available/noVideoSr/noAudio=" +
                    std::to_string(video_clock_available_.load()) + "/" + std::to_string(video_clock_no_video_sr_.load()) + "/" +
                    std::to_string(video_clock_no_audio_.load()) +
                    " statsLockSkips=" +
                    std::to_string(stats_lock_skips_.load()) +
                    " transferStorage reused/fresh=" +
                    std::to_string(decoder_ ? decoder_->transfer_storage_reused() : 0) + "/" +
                    std::to_string(decoder_ ? decoder_->transfer_storage_fresh() : 0) +
                    " frameGuard verified/faults/quarantined/corrupt/noPts/pruned=" +
                    std::to_string(video_frame_gate_.Verified()) + "/" +
                    std::to_string(video_frame_gate_.Faults()) + "/" +
                    std::to_string(video_frame_gate_.Quarantined()) + "/" +
                    std::to_string(video_frame_gate_.Corrupt()) + "/" +
                    std::to_string(video_frame_gate_.UnknownPts()) + "/" +
                    std::to_string(video_frame_gate_.PrunedReady()) +
                    " readyLatencyTrims=" + std::to_string(ready_timing.latency_trims) +
                    " readyLatencyBackoffs=" + std::to_string(ready_timing.latency_backoffs) +
                    " readyDepth 1/2/3plus=" + std::to_string(ready_timing.depth_buckets[1]) + "/" +
                    std::to_string(ready_timing.depth_buckets[2]) + "/" + std::to_string(ready_timing.depth_buckets[3]) +
                    " readyAgeBuckets 8/16/25/33/50/100/overMs=" +
                    std::to_string(ready_timing.age_buckets[0]) + "/" + std::to_string(ready_timing.age_buckets[1]) + "/" +
                    std::to_string(ready_timing.age_buckets[2]) + "/" + std::to_string(ready_timing.age_buckets[3]) + "/" +
                    std::to_string(ready_timing.age_buckets[4]) + "/" + std::to_string(ready_timing.age_buckets[5]) + "/" +
                    std::to_string(ready_timing.age_buckets[6]) +
                    " frameHeaders alloc/reuse/cache=" +
                    std::to_string(frame_headers.allocated) + "/" +
                    std::to_string(frame_headers.reused) + "/" +
                    std::to_string(frame_headers.cached) +
                    " nvdecTransfer count/totalUs/maxUs=" +
                    std::to_string(decoder_ ? decoder_->transfer_count() : 0) + "/" +
                    std::to_string(decoder_ ? decoder_->transfer_us_total() : 0) + "/" +
                    std::to_string(decoder_ ? decoder_->transfer_us_max() : 0) +
                    " in/dec/outFps=" + std::to_string(incoming_fps) + "/" +
                    std::to_string(decoded_fps) + "/" + std::to_string(presented_fps) +
                    " bitrateMbps=" + std::to_string(bitrate_mbps) +
                    " auBytesTotal/max=" + std::to_string(video.access_unit_bytes) + "/" +
                    std::to_string(video_access_unit_max_bytes_.load()) +
                    " decodeUsP95/max=" + std::to_string(video.decode_us_p95) + "/" +
                    std::to_string(video.decode_us_max) +
                    " queueWaitUsP95/max=" + std::to_string(video.queue_wait_us_p95) + "/" +
                    std::to_string(video.queue_wait_us_max) +
                    " renderUsP95/max=" + std::to_string(video.render_us_p95) + "/" +
                    std::to_string(video.render_us_max) +
                    " decodeQueue/current/high=" + std::to_string(video.decode_queue_size) + "/" +
                    std::to_string(video.decode_queue_high_water) +
                    " bufferPool reuse/alloc=" + std::to_string(decoder_buffer_reuses_.load()) + "/" +
                    std::to_string(decoder_buffer_allocations_.load()) +
                    " transport batches/datagrams/max=" +
                    std::to_string(transport_batches_.load()) + "/" +
                    std::to_string(transport_datagrams_.load()) + "/" +
                    std::to_string(transport_batch_high_water_.load()) +
                    " gpu retained/mappings=" +
                    std::to_string(render_backend_stats.retained_surfaces) + "/" +
                    std::to_string(render_backend_stats.surface_mappings) +
                    " uploadDetail immutable/descriptorWrites/submits=" +
                    std::to_string(render_backend_stats.immutable_upload_descriptors ? 1 : 0) + "/" +
                    std::to_string(render_backend_stats.descriptor_updates) + "/" +
                    std::to_string(render_backend_stats.update_submissions) +
                    " memcpyUs total/max=" + std::to_string(render_backend_stats.upload_memcpy_us_total) + "/" +
                    std::to_string(render_backend_stats.upload_memcpy_us_max) +
                    " slotFenceUs total/max=" + std::to_string(render_backend_stats.slot_fence_wait_us_total) + "/" +
                    std::to_string(render_backend_stats.slot_fence_wait_us_max) +
                    " commandFenceUs total/max=" + std::to_string(render_backend_stats.command_fence_wait_us_total) + "/" +
                    std::to_string(render_backend_stats.command_fence_wait_us_max) +
                    " uploadCpu count/totalUs/maxUs=" +
                    std::to_string(render_backend_stats.upload_count) + "/" +
                    std::to_string(render_backend_stats.upload_cpu_us_total) + "/" +
                    std::to_string(render_backend_stats.upload_cpu_us_max) +
                    " rtp packets/gaps/reordered/late/forced/startupRebase/buffered=" +
                    std::to_string(rtp_stats.packets_received) + "/" +
                    std::to_string(rtp_stats.sequence_gaps) + "/" +
                    std::to_string(rtp_stats.reordered_packets) + "/" +
                    std::to_string(rtp_stats.late_packets_dropped) + "/" +
                    std::to_string(rtp_stats.forced_sequence_skips) + "/" +
                    std::to_string(rtp_stats.startup_sequence_rebases) + "/" +
                    std::to_string(rtp_stats.reorder_buffered_packets) +
                    " adaptive hold/recovered/expired/fastForward=" +
                    std::to_string(rtp_stats.reorder_adaptive_holds) + "/" +
                    std::to_string(rtp_stats.reorder_gap_recoveries) + "/" +
                    std::to_string(rtp_stats.reorder_gap_expirations) + "/" +
                    std::to_string(rtp_stats.reorder_burst_fast_forwards) +
                    " grace regular/burst=" +
                    std::to_string(rtp_stats.reorder_gap_hold_ms) + "/" +
                    std::to_string(rtp_stats.reorder_burst_gap_hold_ms) +
                    " nack requests/packets/retries=" +
                    std::to_string(rtp_stats.nack_requests) + "/" +
                    std::to_string(rtp_stats.nack_packets_requested) + "/" +
                    std::to_string(rtp_stats.nack_retries) +
                    " rtx received/recovered/invalid=" +
                    std::to_string(rtp_stats.rtx_packets_received) + "/" +
                    std::to_string(rtp_stats.rtx_packets_recovered) + "/" +
                    std::to_string(rtp_stats.rtx_packets_invalid) +
                    " rtpAu ok/drop=" + std::to_string(rtp_stats.access_units_completed) + "/" +
                    std::to_string(rtp_stats.access_units_dropped) +
                    " rtpDamageTs=" + std::to_string(rtp_stats.last_dropped_timestamp) +
                    " arrival p95Us/jitterUs/over33/over50/over100=" +
                    std::to_string(arrival_p95_us_.load()) + "/" + std::to_string(arrival_jitter_us_.load()) + "/" +
                    std::to_string(arrival_over33_.load()) + "/" + std::to_string(arrival_over50_.load()) + "/" +
                    std::to_string(arrival_over100_.load()) +
                    " arrival timestampGap/lossGap/deliveryGap/burst=" +
                    std::to_string(source_timestamp_gaps_.load()) + "/" +
                    std::to_string(timestamp_gaps_with_loss_.load()) + "/" +
                    std::to_string(delivery_arrival_gaps_.load()) + "/" +
                    std::to_string(delivery_bursts_.load()) +
                    " frameQueue size/dropTiming/dropOverflow/dropStale/hold/reuse=" +
                    std::to_string(frame_holder.getFrameQueueSize()) + "/" +
                    std::to_string(frame_holder.getTimingDropStat()) + "/" +
                    std::to_string(frame_holder.getOverflowDropStat()) + "/" +
                    std::to_string(frame_holder.getStaleAgeDropStat()) + "/" +
                    std::to_string(frame_holder.getTimingHoldStat()) + "/" +
                    std::to_string(frame_holder.getFakeFrameStat()) +
                    " frameCause reuseEmpty/holdEarly/wait/recovered/timeout/predictSkip/startupHold=" +
                    std::to_string(frame_holder.getQueueEmptyReuseStat()) + "/" +
                    std::to_string(frame_holder.getTimingHoldStat()) + "/" +
                    std::to_string(frame_holder.getBriefWaitStat()) + "/" +
                    std::to_string(frame_holder.getBriefWaitRecoveredStat()) + "/" +
                    std::to_string(frame_holder.getBriefWaitTimeoutStat()) + "/" +
                    std::to_string(frame_holder.getPredictiveWaitSkippedStat()) + "/" +
                    std::to_string(frame_holder.getStartupHoldStat()) +
                    " waitUs total/max=" +
                    std::to_string(frame_holder.getBriefWaitTotalUs()) + "/" +
                    std::to_string(frame_holder.getBriefWaitMaximumUs()) +
                    " frameAge count/avgUs/maxUs=" +
                    std::to_string(frame_holder.getPresentationAgeCount()) + "/" +
                    std::to_string(frame_holder.getPresentationAgeCount() == 0 ? 0 :
                        frame_holder.getPresentationAgeTotalUs() /
                            frame_holder.getPresentationAgeCount()) + "/" +
                    std::to_string(frame_holder.getPresentationAgeMaximumUs()) +
                    " producer interval/jitterUs=" +
                    std::to_string(frame_holder.getObservedProducerIntervalUs()) + "/" +
                    std::to_string(frame_holder.getObservedProducerJitterUs()) +
                    " sourcePacing rtpPeriod/early/phase/external/bypass/reuseNow/reuseMax=" +
                    std::to_string(frame_holder.getObservedProducerRtpTicks()) + "/" +
                    std::to_string(frame_holder.getPacingEarlyToleranceTicks()) + "/" +
                    std::to_string(frame_holder.getLastPacingPhaseTicks()) + "/" +
                    std::to_string(frame_holder.getSourceClockPresentStat()) + "/" +
                    std::to_string(frame_holder.getClockBypassPresentStat()) + "/" +
                    std::to_string(frame_holder.getCurrentConsecutiveReuse()) + "/" +
                    std::to_string(frame_holder.getMaximumConsecutiveReuse()) +
                    " dropCause timing/overflow/staleReady/decoderResync/decoderOverflow/decoderStall/corrupt/renderLate/transportGap=" +
                    std::to_string(frame_holder.getTimingDropStat()) + "/" +
                    std::to_string(frame_holder.getOverflowDropStat()) + "/" +
                    std::to_string(frame_holder.getStaleAgeDropStat()) + "/" +
                    std::to_string(decoder_resync_drops_.load()) + "/" +
                    std::to_string(decoder_overflow_drops_.load()) + "/" +
                    std::to_string(decoder_stall_drops_.load()) + "/" +
                    std::to_string(decoder_ ? decoder_->corrupt_frames_dropped() : 0) + "/" +
                    std::to_string(render_late_frames_.load()) + "/" +
                    std::to_string(rtp_stats.sequence_gaps));

    // Buffered files remove open/close/fsync work from packet and render hot
    // paths. Flush once per diagnostic interval so crash logs lose at most a
    // short tail while normal gameplay avoids SD-card metadata stalls.
    RequestDiagnosticFlush();

    if (pc_)
    {
        const auto pairs = opennow::webrtc::CaptureIceCandidatePairs(pc_);
        const std::string signature =
            opennow::webrtc::IceCandidatePairsSignature(pairs);
        if (signature != last_ice_candidate_pair_signature_)
        {
            last_ice_candidate_pair_signature_ = signature;
            for (const auto& line : opennow::webrtc::FormatIceCandidatePairs(pairs))
                AppendStreamLog(line);
        }
    }
}


VideoPerformanceCounters WebRtcSession::get_video_performance() const {
    VideoPerformanceCounters result;
    result.access_units = static_cast<uint64_t>(packets_received_.load(std::memory_order_relaxed));
    result.access_unit_bytes = video_access_unit_bytes_.load(std::memory_order_relaxed);
    result.decoded_frames = static_cast<uint64_t>(frames_decoded_.load(std::memory_order_relaxed));
    result.presented_frames = presented_frames_.load(std::memory_order_relaxed);
    result.decode_us_p95 = LatencyPercentile95(decode_latency_buckets_);
    result.decode_us_max = decode_us_max_.load(std::memory_order_relaxed);
    result.queue_wait_us_p95 = LatencyPercentile95(queue_wait_buckets_);
    result.queue_wait_us_max = queue_wait_us_max_.load(std::memory_order_relaxed);
    result.render_us_p95 = LatencyPercentile95(render_latency_buckets_);
    result.render_us_max = render_us_max_.load(std::memory_order_relaxed);
    result.arrival_p95_us = arrival_p95_us_.load(std::memory_order_relaxed);
    result.arrival_jitter_us = arrival_jitter_us_.load(std::memory_order_relaxed);
    result.arrival_over33 = arrival_over33_.load(std::memory_order_relaxed);
    result.arrival_over50 = arrival_over50_.load(std::memory_order_relaxed);
    result.arrival_over100 = arrival_over100_.load(std::memory_order_relaxed);
    result.repeated_frames = AVFrameHolder::instance().getQueueEmptyReuseStat() +
        AVFrameHolder::instance().getTimingHoldStat();
    result.presentation_drops = AVFrameHolder::instance().getFrameDropStat();
    const auto ready = AVFrameHolder::instance().getReadyMetrics();
    result.ready_count = ready.count; result.ready_age_total_us = ready.age_total_us;
    result.ready_depth3 = ready.depth_buckets[3]; result.latency_trims = ready.latency_trims; result.latency_backoffs=ready.latency_backoffs;
    result.decode_queue_high_water = decoder_queue_high_water_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        result.decode_queue_size = decoder_queue_.size();
    }
    return result;
}

int WebRtcSession::get_network_rtt_ms() const {
    return get_network_rtt_stats().current_ms;
}

ClientPerformanceCounters WebRtcSession::get_client_performance() const {
    ClientPerformanceCounters result;
    result.input_queue_max_us = input_queue_wait_us_max_.load(std::memory_order_relaxed);
    result.input_send_max_us = input_send_us_max_.load(std::memory_order_relaxed);
    result.transport_batch_max_us = transport_batch_us_max_.load(std::memory_order_relaxed);
    result.input_backpressure = input_backpressure_count_.load(std::memory_order_relaxed);
    result.input_interval_max_us = input_interval_us_max_.load(std::memory_order_relaxed);
    result.input_queue_window_us = input_queue_window_.Take();
    result.input_send_window_us = input_send_window_.Take();
    result.transport_batch_window_us = transport_batch_window_.Take();
    result.transport_service_window_us = transport_service_window_.Take();
    result.live_maintenance_window_us = live_maintenance_performance_window_.Take();
    result.live_maintenance_deferrals = live_maintenance_deferrals_.load(std::memory_order_relaxed);
    result.input_idle_wait_skips = input_idle_wait_skips_.load(std::memory_order_relaxed);
    if (audio_) result.audio = audio_->performance();
    return result;
}

NetworkRttStats WebRtcSession::get_network_rtt_stats(bool transport_snapshot) const {
    NetworkRttStats result;
    result.current_ms = cached_network_rtt_ms_.load(std::memory_order_relaxed);
    std::unique_lock<std::recursive_mutex> lock(peer_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        stats_lock_skips_.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> cache_lock(rtt_snapshot_mutex_, std::try_to_lock);
        if (cache_lock.owns_lock() && rtt_snapshot_at_us_) {
            result = rtt_snapshot_;
            const auto age = (NowUs() - rtt_snapshot_at_us_) / 1000;
            result.snapshot_fresh = age <= 500;
            result.snapshot_age_ms = static_cast<int>(std::min<uint64_t>(age, 2147483647));
            result.stun_sample_age_ms = static_cast<uint32_t>(std::min<uint64_t>(
                uint64_t(result.stun_sample_age_ms) + age, 4294967295ULL));
        }
        return result;
    }
    if (!pc_)
        return result;

    PeerNetworkRttStats stats {};
    if (transport_snapshot) {
        // Frequent publication reads only the agent's in-memory STUN counters.
        // SCTP_STATUS may take an internal usrsctp lock; keep that out of this
        // 200ms transport task instead of adding new send-path contention.
        if (peer_connection_get_stun_rtt_stats(pc_, &stats) != 0) return result;
    } else if (peer_connection_get_rtt_stats(pc_, &stats) != 0) return result;

    result.current_ms = stats.current_ms;
    result.stun_current_ms = stats.stun_current_ms;
    result.sctp_current_ms = stats.sctp_current_ms;
    result.min_ms = stats.min_ms;
    result.median_ms = stats.median_ms;
    result.p95_ms = stats.p95_ms;
    result.jitter_ms = stats.jitter_ms;
    result.stun_sample_age_ms = stats.stun_sample_age_ms;
    result.samples = stats.samples;
    result.probes_sent = stats.probes_sent;
    result.probes_matched = stats.probes_matched;
    result.probes_lost = stats.probes_lost;
    result.probes_unmatched = stats.probes_unmatched;
    result.probe_in_flight = stats.probe_in_flight != 0;
    result.source =
        stats.source == PEER_RTT_SOURCE_STUN ? NetworkRttSource::Stun :
        stats.source == PEER_RTT_SOURCE_SCTP ? NetworkRttSource::Sctp :
        NetworkRttSource::None;
    cached_network_rtt_ms_.store(result.current_ms, std::memory_order_relaxed);
    result.snapshot_age_ms = 0;
    result.snapshot_fresh = true;
    { std::unique_lock<std::mutex> cache_lock(rtt_snapshot_mutex_, std::try_to_lock);
      if (cache_lock.owns_lock()) { rtt_snapshot_ = result; rtt_snapshot_at_us_ = NowUs(); } }
    return result;
}

StreamNetworkCounters WebRtcSession::get_network_counters() const {
    StreamNetworkCounters counters;
    counters.packets_received = cached_rtp_packets_.load(std::memory_order_relaxed);
    counters.sequence_gaps = cached_rtp_gaps_.load(std::memory_order_relaxed);
    counters.late_packets_dropped = cached_rtp_late_.load(std::memory_order_relaxed);
    counters.access_units_dropped = cached_rtp_au_dropped_.load(std::memory_order_relaxed);
    counters.nack_requests = cached_rtp_nack_requests_.load(std::memory_order_relaxed);
    counters.reordered = cached_rtp_reordered_.load(std::memory_order_relaxed);
    counters.rtx_recovered = cached_rtp_rtx_.load(std::memory_order_relaxed);
    std::unique_lock<std::recursive_mutex> lock(peer_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        stats_lock_skips_.fetch_add(1, std::memory_order_relaxed);
        return counters;
    }
    PeerVideoRtpStats stats {};
    if (pc_)
        (void)peer_connection_get_video_rtp_stats(pc_, &stats);

    counters.packets_received = stats.packets_received;
    counters.sequence_gaps = stats.sequence_gaps;
    counters.late_packets_dropped = stats.late_packets_dropped;
    counters.access_units_dropped = stats.access_units_dropped;
    counters.nack_requests = stats.nack_requests;
    counters.reordered = stats.reordered_packets;
    counters.rtx_recovered = stats.rtx_packets_recovered;
    cached_rtp_reordered_.store(counters.reordered, std::memory_order_relaxed);
    cached_rtp_rtx_.store(counters.rtx_recovered, std::memory_order_relaxed);
    cached_rtp_packets_.store(counters.packets_received, std::memory_order_relaxed);
    cached_rtp_gaps_.store(counters.sequence_gaps, std::memory_order_relaxed);
    cached_rtp_late_.store(counters.late_packets_dropped, std::memory_order_relaxed);
    cached_rtp_au_dropped_.store(counters.access_units_dropped, std::memory_order_relaxed);
    cached_rtp_nack_requests_.store(counters.nack_requests, std::memory_order_relaxed);
    return counters;
}

StreamTransportHealth WebRtcSession::get_transport_health() const {
    StreamTransportHealth health;
    health.offer_received = offer_received_.load(std::memory_order_acquire);
    health.startup_timed_out = startup_timed_out_.load(std::memory_order_acquire);
    health.input_ready = input_ready_.load(std::memory_order_acquire);
    health.peer_completed = peer_ever_completed_.load(std::memory_order_acquire);
    health.peer_terminal = static_cast<opennow::PeerTerminalKind>(
        peer_terminal_kind_.load(std::memory_order_acquire));
    health.signaling_connected = signaling_client_ && signaling_client_->is_connected();
    const uint64_t last_packet_us = last_video_packet_at_us_.load(std::memory_order_acquire);
    health.video_started = last_packet_us != 0;
    if (last_packet_us != 0) {
        const uint64_t now_us = NowUs();
        health.video_idle = std::chrono::milliseconds(
            now_us > last_packet_us ? (now_us - last_packet_us) / 1000 : 0);
    }
    return health;
}

std::string WebRtcSession::get_debug_info() const {
    std::unique_lock<std::mutex> snapshot_lock(debug_snapshot_mutex_,std::try_to_lock);
    if(!snapshot_lock.owns_lock()) return "Diagnostics snapshot busy";
    const auto snapshot_now=NowUs();
    if(debug_snapshot_at_us_ && snapshot_now>=debug_snapshot_at_us_ &&
        snapshot_now-debug_snapshot_at_us_<250000) return debug_snapshot_;
    std::unique_lock<std::recursive_mutex> lock(peer_mutex_,std::try_to_lock);
    if(!lock.owns_lock()) return debug_snapshot_.empty() ? "Diagnostics snapshot pending" : debug_snapshot_;
    char buf[1800];
    std::string msg_log;
    for (const auto& m : last_messages_) {
        msg_log += m + "\n";
    }
    int pair_total = 0;
    int pair_frozen = 0;
    int pair_checking = 0;
    int pair_succeeded = 0;
    int pair_failed = 0;
    if (pc_) {
        peer_connection_get_ice_candidate_pair_stats(
            pc_, &pair_total, &pair_frozen, &pair_checking, &pair_succeeded, &pair_failed);
    }
    size_t decode_queue_size = 0;
    size_t decode_pool_size = 0;
    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        decode_queue_size = decoder_queue_.size();
        decode_pool_size = decoder_buffer_pool_.size();
    }
    PeerVideoRtpStats rtp_stats {};
    if (pc_)
        (void)peer_connection_get_video_rtp_stats(pc_, &rtp_stats);
    const auto& holder = AVFrameHolder::instance();
    const VideoPerformanceCounters video = get_video_performance();
    snprintf(buf, sizeof(buf),
             "URL: %s\nMedia: %s:%d\nICE servers: %zu\nPreset: %s %dx%d@%d %d kbps\nState: %s\nSignaling RX: %d  Offers: %d  ICE remote/local: %d/%d  HB rx/tx: %d/%d\nICE pairs total/frozen/checking/ok/failed: %d/%d/%d/%d/%d\nDataChannel SCTP/requested/attempts/start: %d/%d/%d/%d\nInput rel/pr/ready/proto/hb/threshold: %d/%d/%d/%d/%d/%d\nXbox reports/mouse clicks: %d/%d\nVideo backend: %s\nVideo access units/bytes/max: %d/%llu/%llu\nDecoded/presented/errors: %d/%llu/%d\nDecode us p95/max: %llu/%llu  queue wait p95/max: %llu/%llu\nRender us p95/max: %llu/%llu\nDecode queue/current/high/dropped: %zu/%zu/%d\nFrame queue: %zu  Dropped: %zu  Reused: %zu\n\nLast Messages:\n%s",
             signaling_url_.c_str(),
             media_ip_.empty() ? "(auto)" : media_ip_.c_str(),
             media_port_,
             ice_servers_.size(),
             settings_.label.c_str(),
             settings_.width,
             settings_.height,
             settings_.fps,
             settings_.bitrate_kbps,
             current_state_.c_str(),
             signaling_rx_count_,
             offer_count_,
             remote_ice_count_,
             local_ice_count_,
             heartbeat_rx_count_,
             heartbeat_tx_count_,
             pair_total,
             pair_frozen,
             pair_checking,
             pair_succeeded,
             pair_failed,
             datachannel_opened_ ? 1 : 0,
             datachannel_open_requested_ ? 1 : 0,
             datachannel_open_attempts_,
             startup_control_sent_ ? 1 : 0,
             reliable_input_channel_requested_ ? 1 : 0,
             partial_input_channel_requested_ ? 1 : 0,
             input_ready_ ? 1 : 0,
             input_protocol_version_.load(std::memory_order_relaxed),
             input_heartbeat_tx_count_,
             partial_reliable_threshold_ms_,
             gamepad_tx_count_,
             mouse_tx_count_,
             video_backend_name_.c_str(),
             packets_received_.load(),
             static_cast<unsigned long long>(video.access_unit_bytes),
             static_cast<unsigned long long>(video_access_unit_max_bytes_.load()),
             frames_decoded_.load(),
             static_cast<unsigned long long>(video.presented_frames),
             decode_errors_.load(),
             static_cast<unsigned long long>(video.decode_us_p95),
             static_cast<unsigned long long>(video.decode_us_max),
             static_cast<unsigned long long>(video.queue_wait_us_p95),
             static_cast<unsigned long long>(video.queue_wait_us_max),
             static_cast<unsigned long long>(video.render_us_p95),
             static_cast<unsigned long long>(video.render_us_max),
             decode_queue_size,
             video.decode_queue_high_water,
             decoder_queue_drops_.load(),
             holder.getFrameQueueSize(),
             holder.getFrameDropStat(),
             holder.getFakeFrameStat(),
             msg_log.c_str());
    std::string result(buf);
    result += "\nAU buffers pool/reuse/alloc: " + std::to_string(decode_pool_size) + "/" +
              std::to_string(decoder_buffer_reuses_.load()) + "/" +
              std::to_string(decoder_buffer_allocations_.load());
    result += "\nRTP packets/gaps/reordered/late/forced/startupRebase: " +
              std::to_string(rtp_stats.packets_received) + "/" +
              std::to_string(rtp_stats.sequence_gaps) + "/" +
              std::to_string(rtp_stats.reordered_packets) + "/" +
              std::to_string(rtp_stats.late_packets_dropped) + "/" +
              std::to_string(rtp_stats.forced_sequence_skips) + "/" +
              std::to_string(rtp_stats.startup_sequence_rebases);
    result += "\nRTP access units ok/dropped: " +
              std::to_string(rtp_stats.access_units_completed) + "/" +
              std::to_string(rtp_stats.access_units_dropped);
    result += "\nRTP adaptive hold/recovered/expired: " +
              std::to_string(rtp_stats.reorder_adaptive_holds) + "/" +
              std::to_string(rtp_stats.reorder_gap_recoveries) + "/" +
              std::to_string(rtp_stats.reorder_gap_expirations);
    result += "\nRTP grace regular/burst ms: " +
              std::to_string(rtp_stats.reorder_gap_hold_ms) + "/" +
              std::to_string(rtp_stats.reorder_burst_gap_hold_ms);
    result += "\nRTCP NACK requests/packets/retries: " +
              std::to_string(rtp_stats.nack_requests) + "/" +
              std::to_string(rtp_stats.nack_packets_requested) + "/" +
              std::to_string(rtp_stats.nack_retries);
    result += "\nFrame timing drop/overflow/hold: " +
              std::to_string(holder.getTimingDropStat()) + "/" +
              std::to_string(holder.getOverflowDropStat()) + "/" +
              std::to_string(holder.getTimingHoldStat());
    result += "\nFrame predictive wait/recovered/skipped/producerUs: " +
              std::to_string(holder.getBriefWaitStat()) + "/" +
              std::to_string(holder.getBriefWaitRecoveredStat()) + "/" +
              std::to_string(holder.getPredictiveWaitSkippedStat()) + "/" +
              std::to_string(holder.getObservedProducerIntervalUs());
    result += "\nFrame age count/avg/max us: " +
              std::to_string(holder.getPresentationAgeCount()) + "/" +
              std::to_string(holder.getPresentationAgeCount() == 0 ? 0 :
                  holder.getPresentationAgeTotalUs() /
                      holder.getPresentationAgeCount()) + "/" +
              std::to_string(holder.getPresentationAgeMaximumUs());
    result += "\nSource pacing rtpPeriod/early/phase/external/bypass/reuseNow/reuseMax: " +
              std::to_string(holder.getObservedProducerRtpTicks()) + "/" +
              std::to_string(holder.getPacingEarlyToleranceTicks()) + "/" +
              std::to_string(holder.getLastPacingPhaseTicks()) + "/" +
              std::to_string(holder.getSourceClockPresentStat()) + "/" +
              std::to_string(holder.getClockBypassPresentStat()) + "/" +
              std::to_string(holder.getCurrentConsecutiveReuse()) + "/" +
              std::to_string(holder.getMaximumConsecutiveReuse());
    if (audio_) {
        result += "\n" + audio_->debug_info();
        const int64_t target = video_target_rtp_timestamp();
        const int64_t rendered = last_rendered_video_pts_.load();
        if (target != AV_NOPTS_VALUE && rendered != AV_NOPTS_VALUE) {
            const int32_t delta_ticks = static_cast<int32_t>(
                static_cast<uint32_t>(rendered) - static_cast<uint32_t>(target));
            result += "\nA/V delta: " + std::to_string(delta_ticks / 90) + " ms";
        } else {
            result += "\nA/V delta: waiting for RTCP SR";
        }
    }
    debug_snapshot_=result; debug_snapshot_at_us_=snapshot_now;
    return result;
}

std::string WebRtcSession::get_adaptation_info() const {
    return "Mode: " + settings_.image_quality_mode + " | " +
        opennow::streaming::PressureName(static_cast<opennow::streaming::Pressure>(adaptation_pressure_.load())) +
        " | wait " + std::to_string(adaptation_wait_us_.load()) + " us";
}
