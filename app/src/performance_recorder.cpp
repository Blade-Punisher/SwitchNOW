#include "performance_recorder.hpp"
#include <algorithm>
#include <sstream>

namespace opennow::performance {
namespace {
constexpr auto names = std::to_array<const char*>({
    "elapsed_ms", "transport_epoch", "window_ms", "target_fps", "incoming_fps_x100",
    "decoded_fps_x100", "displayed_fps_x100", "bitrate_kbps", "rtt_ms", "rtt_median_ms",
    "rtt_p95_ms", "rtt_jitter_ms", "rtt_samples_epoch", "rtt_source_code", "stun_sample_age_ms",
    "gaps_new", "late_new", "damaged_frames_new", "nack_new", "decode_p95_epoch_us",
    "decode_max_epoch_us", "queue_wait_p95_epoch_us", "render_p95_epoch_us", "render_max_epoch_us",
    "decoder_queue_now", "ui_tick_max_window_us", "ui_ticks_over25ms_new", "input_queue_max_epoch_us",
    "input_send_max_epoch_us", "input_interval_max_epoch_us", "transport_batch_max_epoch_us", "input_backpressure_new",
    "transport_pauses_new", "transport_pause_ms_new", "source_pauses_new", "assembly_pauses_new", "uncertain_pauses_new",
    "playback_pauses_new", "playback_pause_ms_new", "audio_plc_new", "audio_underruns_new",
    "audio_errors_new", "audio_queued_ms", "input_ready", "peer_ready", "video_started",
    "assessment_code", "rtt_snapshot_age_ms", "rtt_snapshot_fresh",
    "input_queue_max_window_us", "input_send_max_window_us", "transport_batch_max_window_us",
    "transport_service_max_window_us", "stream_warming_up", "audio_arrival_jitter_us",
    "audio_restart_target_ms", "audio_limited_blocks_new", "audio_gain_x100", "attempt_id",
    "transport_terminal", "recovering", "incoming_units_new", "decoded_units_new", "presented_units_new",
    "rtp_packets_new", "reordered_new", "rtx_recovered_new", "repeated_frames_new", "presentation_drops_new",
    "arrival_p95_epoch_us", "arrival_jitter_us", "arrival_over33ms_new", "arrival_over50ms_new", "arrival_over100ms_new",
    "ready_frames_new", "ready_age_total_us_new", "ready_depth3_new", "latency_trims_new", "latency_backoffs_new", "ui_logic_max_window_us", "ui_frame_max_window_us",
    "live_maintenance_max_window_us", "live_maintenance_deferrals_new", "input_idle_wait_skips_new", "trigger_flags"});
static_assert(names.size() == Count);
void Header(std::ostream& out) {
    for (unsigned i = 0; i < Count; ++i) { if (i) out << ','; out << names[i]; }
    out << '\n';
}
void Values(std::ostream& out, const Row& row) {
    for (unsigned i = 0; i < Count; ++i) { if (i) out << ','; out << row[i]; }
    out << '\n';
}
}
unsigned FlagsFor(const Row& r) {
    if (r[Terminal]) return 0; // terminal UI countdown is not gameplay performance
    unsigned flags = 0;
    const bool visible_impact = r[TargetFps] > 0 &&
        (r[DisplayedX100] < r[TargetFps] * 90 || r[IncomingX100] < r[TargetFps] * 90 ||
         r[PlaybackPauses] || r[AudioUnderruns]);
    if (r[Damaged] || r[TransportPauses] || (r[Gaps] && visible_impact)) flags |= Network;
    if (r[Late] && visible_impact) flags |= Delivery;
    if (r[VideoStarted] && !r[WarmingUp]) {
        if (r[TargetFps] > 0 && r[IncomingX100] < r[TargetFps] * 85) flags |= Incoming;
        if (r[DecodedX100] + 500 < r[IncomingX100]) flags |= Decoder;
        if (r[DisplayedX100] + 500 < r[DecodedX100] || r[PlaybackPauses]) flags |= Display;
    }
    if (r[AudioUnderruns] || r[AudioErrors]) flags |= Audio;
    const auto ui_limit = r[TargetFps] > 0
        ? std::max<std::int64_t>(25000, 1500000 / r[TargetFps]) : 25000;
    if (r[UiTicksOver25Ms] && r[UiTickMaxUs] > ui_limit) flags |= Ui;
    if (r[RttSnapshotFresh] && (r[RttSourceCode] != 1 || r[RttAgeMs] <= 2000) &&
        r[RttMs] > 0 && r[RttMedianMs] > 0 &&
        r[RttMs] > r[RttMedianMs] + 20) flags |= Rtt;
    if (r[InputBackpressure] || (r[PeerReady] && !r[InputReady])) flags |= Controls;
    if (r[VideoStarted] && !r[WarmingUp] && (r[InputQueueWindowUs] >= 25000 ||
        r[InputSendWindowUs] >= 25000 || r[TransportBatchWindowUs] >= 25000 ||
        r[TransportServiceWindowUs] >= 25000)) flags |= ClientStall;
    return flags;
}
unsigned SeverityFor(const Row& row) {
    if (!FlagsFor(row)) return 0;
    if ((row[TargetFps] > 0 && row[VideoStarted] && !row[WarmingUp] &&
         row[DisplayedX100] < row[TargetFps] * 50) || row[PlaybackPauseMs] >= 250)
        return 3;
    if (FlagsFor(row) & (Incoming | Decoder | Display | Audio | Ui | Rtt | Controls | ClientStall | Delivery)) return 2;
    return 1;
}
std::string CsvHeader(bool incidents) {
    std::ostringstream out;
    if (incidents) out << "incident_id,incident_flags,incident_severity,context_index,";
    Header(out);
    return out.str();
}
void Recorder::ObserveTick(std::uint64_t now) {
    if (last_tick_ && now >= last_tick_) {
        const auto interval = now - last_tick_;
        ui_max_ = std::max(ui_max_, interval);
        if (interval > 25000) { ++ui_slow_; ++ui_slow_total_; }
    }
    last_tick_ = now;
}
void Recorder::Record(Row row) {
    row[UiTickMaxUs] = ui_max_; row[UiTicksOver25Ms] = ui_slow_;
    ui_max_ = ui_slow_ = 0;
    if (row[Epoch] != epoch_) {
        ++transport_epochs_;
        epoch_ = row[Epoch]; epoch_samples_ = 0; last_tick_ = 0; first_video_ms_ = -1;
    }
    if (!row[VideoStarted]) first_video_ms_ = -1;
    else if (first_video_ms_ < 0) first_video_ms_ = row[TimeMs];
    row[WarmingUp] = first_video_ms_ < 0 || row[TimeMs] - first_video_ms_ < 2000;
    ++epoch_samples_;
    row[Flags] = FlagsFor(row);
    for (const auto field : {IncomingUnits, DecodedUnits, PresentedUnits, RtpPackets, Gaps, Late, Damaged,
                            Reordered, RtxRecovered, RepeatedFrames, PresentationDrops, PlaybackPauses,
                            AudioUnderruns, TransportPauses, PlaybackPauseMs, TransportPauseMs,
                            ArrivalOver33, ArrivalOver50, ArrivalOver100, ReadyFrames, ReadyAgeTotalUs, ReadyDepth3, LatencyTrims, LatencyBackoffs,
                            LiveMaintenanceDeferrals, InputIdleWaitSkips})
        totals_[field] += std::max<std::int64_t>(0, row[field]);
    if (row[VideoStarted] && row[Terminal])
        excluded_terminal_ms_ += std::max<std::int64_t>(0, row[WindowMs]);
    if (row[VideoStarted]) {
        audio_underruns_total_ += std::max<std::int64_t>(0, row[AudioUnderruns]);
        playback_pause_ms_total_ += std::max<std::int64_t>(0, row[PlaybackPauseMs]);
        transport_pause_ms_total_ += std::max<std::int64_t>(0, row[TransportPauseMs]);
    }
    if (!row[Terminal] && !row[WarmingUp] && row[VideoStarted] && row[TargetFps] > 0 && row[WindowMs] > 0) {
        const auto ms = static_cast<std::uint64_t>(row[WindowMs]);
        observed_video_ms_ += ms;
        incoming_area_ += std::max<std::int64_t>(0, row[IncomingX100]) * ms;
        decoded_area_ += std::max<std::int64_t>(0, row[DecodedX100]) * ms;
        displayed_area_ += std::max<std::int64_t>(0, row[DisplayedX100]) * ms;
        if (row[IncomingX100] < row[TargetFps] * 90) low_incoming_ms_ += ms;
        if (row[DisplayedX100] < row[TargetFps] * 90) low_display_ms_ += ms;
    }
    // Finish pending post-event context before considering a new incident.
    for (auto& event : events_) if (event.after) {
        if (event.size < kContext) event.rows[event.size++] = row;
        --event.after;
    }
    if (epoch_samples_ > 2 && row[Flags]) {
        ++slow_windows_;
        // Cluster consecutive bad windows; retain at most one context per 6s.
        const unsigned severity = SeverityFor(row);
        const auto group = severity - 1;
        if (row[TimeMs] - last_incident_ms_[group] >= 6000) {
            // Reserve 4 critical, 8 moderate and 4 minor slots. A late packet
            // at 60 FPS cannot evict a 15 FPS freeze from a different class.
            constexpr unsigned starts[] = {12, 4, 0}, capacities[] = {4, 8, 4};
            auto& event = events_[starts[group] + event_heads_[group]];
            event = {};
            event.id = ++incidents_;
            event.flags = static_cast<unsigned>(row[Flags]);
            event.severity = severity;
            const unsigned before = std::min(size_, 3u);
            for (unsigned i = before; i > 0; --i)
                event.rows[event.size++] = history_[(head_ + kHistory - i) % kHistory];
            event.rows[event.size++] = row;
            event.after = 3;
            event_heads_[group] = (event_heads_[group] + 1) % capacities[group];
            last_incident_ms_[group] = row[TimeMs];
        }
    }
    history_[head_] = row;
    head_ = (head_ + 1) % kHistory;
    size_ = std::min(size_ + 1, kHistory);
    ++samples_;
}
std::string Recorder::TimelineCsv() const {
    std::ostringstream out; Header(out);
    for (unsigned i = size_; i > 0; --i) Values(out, history_[(head_ + kHistory - i) % kHistory]);
    return out.str();
}
std::string Recorder::IncidentsCsv() const {
    std::ostringstream out; out << CsvHeader(true);
    std::array<const Incident*, kIncidents> ordered {};
    unsigned count = 0;
    for (const auto& event : events_) if (event.id) ordered[count++] = &event;
    std::sort(ordered.begin(), ordered.begin() + count,
        [](auto* left, auto* right) { return left->id < right->id; });
    for (unsigned i = 0; i < count; ++i) {
        const auto& event = *ordered[i];
        for (unsigned j = 0; j < event.size; ++j) {
            out << event.id << ',' << event.flags << ',' << event.severity << ',' << j << ',';
            Values(out, event.rows[j]);
        }
    }
    return out.str();
}
std::string Recorder::Summary() const {
    std::array<unsigned, 4> retained {};
    for (const auto& event : events_) if (event.id) ++retained[event.severity];
    return "session_metrics performance_samples=" + std::to_string(samples_) +
        " flagged_windows=" + std::to_string(slow_windows_) +
        " incident_clusters=" + std::to_string(incidents_) +
        " ui_ticks_over25ms=" + std::to_string(ui_slow_total_) +
        " retained_critical=" + std::to_string(retained[3]) +
        " retained_moderate=" + std::to_string(retained[2]) +
        " retained_minor=" + std::to_string(retained[1]) +
        " history_limit=" + std::to_string(kHistory) + " incident_limit=" + std::to_string(kIncidents) + "\n" +
        "session_metrics observed_video_ms=" + std::to_string(observed_video_ms_) +
        " mean_incoming_fps_x100=" + std::to_string(observed_video_ms_ ? incoming_area_ / observed_video_ms_ : 0) +
        " mean_decoded_fps_x100=" + std::to_string(observed_video_ms_ ? decoded_area_ / observed_video_ms_ : 0) +
        " mean_displayed_fps_x100=" + std::to_string(observed_video_ms_ ? displayed_area_ / observed_video_ms_ : 0) +
        " low_incoming_ms=" + std::to_string(low_incoming_ms_) + " low_display_ms=" + std::to_string(low_display_ms_) +
        " audio_underruns_total=" + std::to_string(audio_underruns_total_) +
        " playback_pause_ms_total=" + std::to_string(playback_pause_ms_total_) +
        " transport_pause_ms_total=" + std::to_string(transport_pause_ms_total_) + "\n" +
        "session_metrics counter_scope=sampled_windows_all_transports transport_epochs=" + std::to_string(transport_epochs_) +
        " excluded_terminal_ms=" + std::to_string(excluded_terminal_ms_) +
        " frames_in_total=" + std::to_string(totals_[IncomingUnits]) +
        " decoded_total=" + std::to_string(totals_[DecodedUnits]) + " displayed_total=" + std::to_string(totals_[PresentedUnits]) +
        " rtp_packets_total=" + std::to_string(totals_[RtpPackets]) + " sequence_gaps_total=" + std::to_string(totals_[Gaps]) +
        " late_packets_total=" + std::to_string(totals_[Late]) + " damaged_units_total=" + std::to_string(totals_[Damaged]) +
        " reordered_total=" + std::to_string(totals_[Reordered]) + " rtx_recovered_total=" + std::to_string(totals_[RtxRecovered]) +
        " repeated_frames_total=" + std::to_string(totals_[RepeatedFrames]) +
        " presentation_drops_total=" + std::to_string(totals_[PresentationDrops]) +
        " arrival_over33ms_total=" + std::to_string(totals_[ArrivalOver33]) +
        " arrival_over50ms_total=" + std::to_string(totals_[ArrivalOver50]) +
        " arrival_over100ms_total=" + std::to_string(totals_[ArrivalOver100]) +
        " ready_frames_total=" + std::to_string(totals_[ReadyFrames]) +
        " ready_age_mean_us=" + std::to_string(totals_[ReadyFrames] ? totals_[ReadyAgeTotalUs] / totals_[ReadyFrames] : 0) +
        " ready_depth3_total=" + std::to_string(totals_[ReadyDepth3]) +
        " latency_trims_total=" + std::to_string(totals_[LatencyTrims]) +
        " latency_backoffs_total=" + std::to_string(totals_[LatencyBackoffs]) +
        " live_maintenance_deferrals_total=" + std::to_string(totals_[LiveMaintenanceDeferrals]) +
        " input_idle_wait_skips_total=" + std::to_string(totals_[InputIdleWaitSkips]) + "\n";
}
} // namespace opennow::performance
