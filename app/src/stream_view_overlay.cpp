#include "StreamView.hpp"
#include "localization.hpp"
#include "server_assignment_policy.hpp"
#include "network_utils.hpp"
#include "stream_diagnostics.hpp"
#include "stream_settings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

void StreamView::DrawDebugOverlay(NVGcontext* vg, float x, float y, float width) {
    if (!session_)
        return;

    std::string debug_text = session_->get_adaptation_info() + "\n" + session_->get_debug_info();
    nvgFontSize(vg, 24.0f);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextBox(vg, x + 50.0f, y + 50.0f, width - 100.0f, debug_text.c_str(), nullptr);
}

void StreamView::DrawPreparingStream(
    NVGcontext* vg, float x, float y, float width, float height) {
    constexpr float kPi = 3.14159265358979323846f;
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    const StreamTransportHealth health = session_
        ? session_->get_transport_health()
        : StreamTransportHealth {};
    const VideoPerformanceCounters counters = session_
        ? session_->get_video_performance()
        : VideoPerformanceCounters {};

    const auto connection_stage = opennow::status::ConnectionStage(
        health.signaling_connected, health.peer_completed, health.input_ready,
        health.video_started, counters.access_units, counters.decoded_frames);
    const int stage = connection_stage == opennow::status::Stage::Ready ? 2
        : connection_stage >= opennow::status::Stage::Video ? 1 : 0;
    const auto title = opennow::Tr(opennow::status::StageTitle(connection_stage));
    const auto detail = opennow::Tr(opennow::status::StageDetail(connection_stage));

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(10, 13, 17));
    nvgFill(vg);

    const float glow_x = x + width * 0.5f + static_cast<float>(std::sin(seconds * 0.45)) * 85.0f;
    const float glow_y = y + height * 0.58f + static_cast<float>(std::cos(seconds * 0.38)) * 42.0f;
    nvgBeginPath(vg);
    nvgCircle(vg, glow_x, glow_y, 210.0f);
    nvgFillColor(vg, nvgRGBA(42, 204, 116, 12));
    nvgFill(vg);

    const float panel_width = std::min(680.0f, width - 72.0f);
    const float panel_height = 410.0f;
    const float panel_x = x + (width - panel_width) * 0.5f;
    const float panel_y = y + (height - panel_height) * 0.5f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, panel_x, panel_y, panel_width, panel_height, 20.0f);
    nvgFillColor(vg, nvgRGBA(16, 18, 22, 248));
    nvgFill(vg);

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, nvgRGB(77, 218, 130));
    const std::string loading_label = opennow::Tr("NOW LOADING");
    nvgText(vg, panel_x + 42.0f, panel_y + 45.0f, loading_label.c_str(), nullptr);
    nvgFontSize(vg, 28.0f);
    nvgFillColor(vg, nvgRGB(241, 245, 247));
    const std::string game = game_title_.empty() ? opennow::Tr("GeForce NOW session") : game_title_;
    nvgText(vg, panel_x + 42.0f, panel_y + 82.0f, game.c_str(), nullptr);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, nvgRGB(142, 150, 160));
    const std::string connection = "GeForce NOW  /  " + opennow::routing::ShortHost(actual_server_) +
        (server_redirects_ ? "  /  " + opennow::Tr("Server reassigned") : "");
    nvgText(vg, panel_x + 42.0f, panel_y + 112.0f, connection.c_str(), nullptr);

    static constexpr const char* kSteps[] = {"CONNECT", "VIDEO", "READY"};
    const float rail_y = panel_y + 178.0f;
    const float first_x = panel_x + 88.0f;
    const float gap = (panel_width - 176.0f) * 0.5f;
    nvgBeginPath(vg);
    nvgRect(vg, first_x, rail_y - 2.0f, gap * 2.0f, 4.0f);
    nvgFillColor(vg, nvgRGB(37, 41, 47));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, first_x, rail_y - 2.0f, gap * std::min(stage, 2), 4.0f);
    nvgFillColor(vg, nvgRGB(77, 218, 130));
    nvgFill(vg);

    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.1));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    for (int i = 0; i < 3; ++i)
    {
        const bool complete = i < stage;
        const bool active = i == stage;
        const float cx = first_x + i * gap;
        if (active)
        {
            nvgBeginPath(vg);
            nvgCircle(vg, cx, rail_y, 29.0f + pulse * 3.0f);
            nvgFillColor(vg, nvgRGBA(77, 218, 130, 22 + static_cast<int>(pulse * 24.0f)));
            nvgFill(vg);
        }
        nvgBeginPath(vg);
        nvgCircle(vg, cx, rail_y, 22.0f);
        nvgFillColor(vg, complete || active ? nvgRGB(77, 218, 130) : nvgRGB(24, 28, 33));
        nvgFill(vg);
        nvgStrokeWidth(vg, 2.0f);
        nvgStrokeColor(vg, complete || active ? nvgRGB(108, 235, 153) : nvgRGB(43, 48, 55));
        nvgStroke(vg);
        nvgFontSize(vg, 16.0f);
        nvgFillColor(vg, complete || active ? nvgRGB(8, 35, 22) : nvgRGB(95, 101, 111));
        const std::string number = std::to_string(i + 1);
        nvgText(vg, cx, rail_y, number.c_str(), nullptr);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, complete || active ? nvgRGB(225, 232, 229) : nvgRGB(91, 96, 105));
        const auto step = opennow::Tr(kSteps[i]);
        nvgText(vg, cx, rail_y + 42.0f, step.c_str(), nullptr);
    }

    const float spinner_x = panel_x + panel_width * 0.5f;
    const float spinner_y = panel_y + 278.0f;
    const float angle = static_cast<float>(
        std::fmod(seconds * 3.0, static_cast<double>(kPi) * 2.0));
    nvgBeginPath(vg);
    nvgCircle(vg, spinner_x, spinner_y, 19.0f);
    nvgStrokeWidth(vg, 5.0f);
    nvgStrokeColor(vg, nvgRGB(35, 43, 48));
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgArc(vg, spinner_x, spinner_y, 19.0f, angle, angle + kPi * 1.42f, NVG_CW);
    nvgStrokeWidth(vg, 5.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeColor(vg, nvgRGB(77, 218, 130));
    nvgStroke(vg);
    const float head_angle = angle + kPi * 1.42f;
    nvgBeginPath(vg);
    nvgCircle(vg, spinner_x + std::cos(head_angle) * 19.0f,
              spinner_y + std::sin(head_angle) * 19.0f, 3.4f);
    nvgFillColor(vg, nvgRGB(123, 242, 166));
    nvgFill(vg);

    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 20.0f);
    nvgFillColor(vg, nvgRGB(238, 242, 245));
    nvgText(vg, spinner_x, panel_y + 330.0f, title.c_str(), nullptr);
    nvgFontSize(vg, 15.0f);
    nvgFillColor(vg, nvgRGB(145, 153, 164));
    nvgText(vg, spinner_x, panel_y + 361.0f, detail.c_str(), nullptr);
    const auto wait = std::chrono::duration_cast<std::chrono::seconds>(
        now - initial_transport_started_at_).count();
    nvgFontSize(vg, 13.0f);
    const auto controls = opennow::Tr(wait >= 10
        ? "A: Retry connection   B: Menu (keep game)"
        : "B: Menu (keep game)");
    nvgText(vg, spinner_x, panel_y + 390.0f, controls.c_str(), nullptr);
}

void StreamView::UpdatePerformanceCounter() {
    const auto now = std::chrono::steady_clock::now();
    if (fps_window_started_.time_since_epoch().count() == 0) {
        fps_window_started_ = now;
        previous_video_counters_ = session_
            ? session_->get_video_performance() : VideoPerformanceCounters {};
        return;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - fps_window_started_);
    if (elapsed >= std::chrono::milliseconds(750)) {
        const VideoPerformanceCounters counters = session_
            ? session_->get_video_performance() : VideoPerformanceCounters {};
        const float scale = elapsed.count() > 0 ? 1000.0f / static_cast<float>(elapsed.count()) : 0.0f;
        const auto delta = [](uint64_t current, uint64_t previous) {
            return current >= previous ? current - previous : uint64_t {0};
        };
        incoming_fps_ = static_cast<float>(
            delta(counters.access_units, previous_video_counters_.access_units)) * scale;
        decoded_fps_ = static_cast<float>(
            delta(counters.decoded_frames, previous_video_counters_.decoded_frames)) * scale;
        presented_fps_ = static_cast<float>(
            delta(counters.presented_frames, previous_video_counters_.presented_frames)) * scale;
        stream_bitrate_mbps_ = static_cast<float>(
            delta(counters.access_unit_bytes, previous_video_counters_.access_unit_bytes)) *
            scale * 8.0f / 1000000.0f;
        const auto rtt = session_ ? session_->get_network_rtt_stats() : NetworkRttStats {};
        network_rtt_ms_ = rtt.current_ms;
        const auto network = session_ ? session_->get_network_counters() : StreamNetworkCounters {};
        recent_video_drops_ = static_cast<uint32_t>(delta(
            network.access_units_dropped, network_counters_.access_units_dropped));
        if (counters.decoded_frames > 0) {
            const opennow::status::Sample sample {
                incoming_fps_, decoded_fps_, presented_fps_, session_settings_.fps,
                static_cast<uint32_t>(delta(network.sequence_gaps, network_counters_.sequence_gaps)),
                static_cast<uint32_t>(delta(network.late_packets_dropped, network_counters_.late_packets_dropped)),
                recent_video_drops_, counters.decode_us_p95,
                static_cast<unsigned>(counters.decode_queue_size)};
            if (performance_assessment_.Observe(opennow::status::Evaluate(sample)))
                session_->record_ui_event(std::string("performance_assessment=") +
                    opennow::status::Label(performance_assessment_.current));
        }
        if (session_) {
            namespace pf = opennow::performance;
            const auto pauses = session_->get_live_pause_stats();
            const auto client = session_->get_client_performance();
            const auto health = session_->get_transport_health();
            pf::Row row {};
            row[pf::TimeMs] = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - initial_transport_started_at_).count();
            row[pf::Epoch] = performance_transport_epoch_;
            row[pf::AttemptId] = session_->diagnostic_attempt_id();
            row[pf::WindowMs] = elapsed.count();
            row[pf::TargetFps] = session_settings_.fps;
            row[pf::IncomingX100] = std::lround(incoming_fps_ * 100);
            row[pf::DecodedX100] = std::lround(decoded_fps_ * 100);
            row[pf::DisplayedX100] = std::lround(presented_fps_ * 100);
            row[pf::BitrateKbps] = std::lround(stream_bitrate_mbps_ * 1000);
            row[pf::RttMs] = rtt.current_ms;
            row[pf::RttMedianMs] = rtt.median_ms;
            row[pf::RttP95Ms] = rtt.p95_ms;
            row[pf::RttJitterMs] = rtt.jitter_ms;
            row[pf::RttSamples] = rtt.samples;
            row[pf::RttSourceCode] = static_cast<int>(rtt.source);
            row[pf::RttSnapshotAgeMs] = rtt.snapshot_age_ms;
            row[pf::RttSnapshotFresh] = rtt.snapshot_fresh;
            row[pf::RttAgeMs] = rtt.stun_sample_age_ms;
            row[pf::Gaps] = delta(network.sequence_gaps, network_counters_.sequence_gaps);
            row[pf::Late] = delta(network.late_packets_dropped, network_counters_.late_packets_dropped);
            row[pf::Damaged] = recent_video_drops_;
            row[pf::Nack] = delta(network.nack_requests, network_counters_.nack_requests);
            row[pf::DecodeP95Us] = counters.decode_us_p95;
            row[pf::DecodeMaxUs] = counters.decode_us_max;
            row[pf::QueueWaitP95Us] = counters.queue_wait_us_p95;
            row[pf::RenderP95Us] = counters.render_us_p95;
            row[pf::RenderMaxUs] = counters.render_us_max;
            row[pf::DecodeDepth] = counters.decode_queue_size;
            row[pf::InputQueueMaxUs] = client.input_queue_max_us;
            row[pf::InputSendMaxUs] = client.input_send_max_us;
            row[pf::InputIntervalMaxUs] = client.input_interval_max_us;
            row[pf::TransportBatchMaxUs] = client.transport_batch_max_us;
            row[pf::InputQueueWindowUs] = client.input_queue_window_us;
            row[pf::InputSendWindowUs] = client.input_send_window_us;
            row[pf::TransportBatchWindowUs] = client.transport_batch_window_us;
            row[pf::TransportServiceWindowUs] = client.transport_service_window_us;
            row[pf::LiveMaintenanceWindowUs] = client.live_maintenance_window_us;
            row[pf::LiveMaintenanceDeferrals] = delta(client.live_maintenance_deferrals, previous_client_stats_.live_maintenance_deferrals);
            row[pf::InputIdleWaitSkips] = delta(client.input_idle_wait_skips, previous_client_stats_.input_idle_wait_skips);
            row[pf::InputBackpressure] = delta(client.input_backpressure, previous_client_stats_.input_backpressure);
            row[pf::TransportPauses] = delta(pauses.transport_count, previous_pause_stats_.transport_count);
            row[pf::TransportPauseMs] = delta(pauses.transport_duration_ms, previous_pause_stats_.transport_duration_ms);
            row[pf::SourcePauses] = delta(pauses.source_count, previous_pause_stats_.source_count);
            row[pf::AssemblyPauses] = delta(pauses.assembly_count, previous_pause_stats_.assembly_count);
            row[pf::UncertainPauses] = delta(pauses.uncertain_count, previous_pause_stats_.uncertain_count);
            row[pf::PlaybackPauses] = delta(pauses.playback_count, previous_pause_stats_.playback_count);
            row[pf::PlaybackPauseMs] = delta(pauses.playback_duration_ms, previous_pause_stats_.playback_duration_ms);
            row[pf::AudioPlc] = delta(client.audio.plc, previous_client_stats_.audio.plc);
            row[pf::AudioUnderruns] = delta(client.audio.underruns, previous_client_stats_.audio.underruns);
            row[pf::AudioErrors] = delta(client.audio.errors, previous_client_stats_.audio.errors);
            row[pf::AudioQueuedMs] = client.audio.queued_ms;
            row[pf::AudioArrivalJitterUs] = client.audio.arrival_jitter_us;
            row[pf::AudioStartTargetMs] = client.audio.start_target_ms;
            row[pf::AudioLimitedBlocks] = delta(client.audio.limited_blocks, previous_client_stats_.audio.limited_blocks);
            row[pf::AudioGainX100] = client.audio.gain_x100;
            row[pf::InputReady] = health.input_ready;
            row[pf::PeerReady] = health.peer_completed;
            row[pf::VideoStarted] = health.video_started;
            row[pf::Terminal] = health.peer_terminal != opennow::PeerTerminalKind::None;
            row[pf::Recovering] = recovering_;
            row[pf::IncomingUnits] = delta(counters.access_units, previous_video_counters_.access_units);
            row[pf::DecodedUnits] = delta(counters.decoded_frames, previous_video_counters_.decoded_frames);
            row[pf::PresentedUnits] = delta(counters.presented_frames, previous_video_counters_.presented_frames);
            row[pf::RtpPackets] = delta(network.packets_received, network_counters_.packets_received);
            row[pf::Reordered] = delta(network.reordered, network_counters_.reordered);
            row[pf::RtxRecovered] = delta(network.rtx_recovered, network_counters_.rtx_recovered);
            row[pf::RepeatedFrames] = delta(counters.repeated_frames, previous_video_counters_.repeated_frames);
            row[pf::PresentationDrops] = delta(counters.presentation_drops, previous_video_counters_.presentation_drops);
            row[pf::ArrivalP95Us] = counters.arrival_p95_us;
            row[pf::ArrivalJitterUs] = counters.arrival_jitter_us;
            row[pf::ArrivalOver33] = delta(counters.arrival_over33, previous_video_counters_.arrival_over33);
            row[pf::ArrivalOver50] = delta(counters.arrival_over50, previous_video_counters_.arrival_over50);
            row[pf::ArrivalOver100] = delta(counters.arrival_over100, previous_video_counters_.arrival_over100);
            row[pf::ReadyFrames] = delta(counters.ready_count, previous_video_counters_.ready_count);
            row[pf::ReadyAgeTotalUs] = delta(counters.ready_age_total_us, previous_video_counters_.ready_age_total_us);
            row[pf::ReadyDepth3] = delta(counters.ready_depth3, previous_video_counters_.ready_depth3);
            row[pf::UiLogicMaxUs] = ui_logic_max_us_;
            row[pf::UiFrameMaxUs] = ui_frame_max_us_;
            ui_logic_max_us_=0; ui_frame_max_us_=0;
            row[pf::LatencyBackoffs] = delta(counters.latency_backoffs, previous_video_counters_.latency_backoffs);
            row[pf::LatencyTrims] = delta(counters.latency_trims, previous_video_counters_.latency_trims);
            row[pf::Assessment] = static_cast<int>(performance_assessment_.current);
            performance_recorder_.Record(row);
            previous_pause_stats_ = pauses;
            previous_client_stats_ = client;
        }
        network_counters_ = network;
        previous_video_counters_ = counters;
        fps_window_started_ = now;
    }
}

void StreamView::DrawPerformanceOverlay(NVGcontext* vg, float x, float y) {
    char text[128];
    if (network_rtt_ms_ >= 0) {
        std::snprintf(
            text, sizeof(text), "FPS  %.0f     BITRATE  %.1f Mbps     PING  %d ms",
            presented_fps_, stream_bitrate_mbps_, network_rtt_ms_);
    } else {
        std::snprintf(
            text, sizeof(text), "FPS  %.0f     BITRATE  %.1f Mbps     PING  -- ms",
            presented_fps_, stream_bitrate_mbps_);
    }

    const float box_x = x + 16.0f;
    const float box_y = y + 16.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 570.0f, debug_diagnostics_ ? 94.0f : 46.0f, 8.0f);
    nvgFillColor(vg, nvgRGBA(8, 12, 14, 205));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x + 6.0f, box_y + 7.0f, 4.0f, 32.0f, 2.0f);
    nvgFillColor(vg, nvgRGB(55, 220, 125));
    nvgFill(vg);

    nvgFontSize(vg, 18.0f);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFillColor(vg, nvgRGB(245, 250, 247));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, box_x + 20.0f, box_y + 23.0f, text, nullptr);
    if (debug_diagnostics_) {
        const std::string backend = opennow::Tr("Video decoder") + ": " +
            (session_ ? session_->video_decoder_label() : "--");
        nvgFontSize(vg, 16.0f);
        nvgText(vg, box_x + 20.0f, box_y + 51.0f, backend.c_str(), nullptr);
        const auto assessment = opennow::TrFormat("Current: {0}",
            {opennow::Tr(opennow::status::Label(performance_assessment_.current))});
        nvgFontSize(vg, 14.0f);
        nvgText(vg, box_x + 20.0f, box_y + 77.0f, assessment.c_str(), nullptr);
    }
}

void StreamView::SetStreamOverlayVisible(bool visible)
{
    if (stream_overlay_visible_ == visible)
        return;

    stream_overlay_visible_ = visible;
    stream_overlay_b_was_down_ = false;
    stream_overlay_a_was_down_ = false;
    stream_overlay_up_was_down_ = false;
    stream_overlay_down_was_down_ = false;
    ResetControllerDeliveryState();

    if (touch_was_down_ && session_)
    {
        session_->send_mouse_left_button(false);
        touch_was_down_ = false;
    }
    if (session_)
    {
        SendNeutralControllerReports();
        session_->record_ui_event(
            visible ? "stream overlay opened by Minus+Plus"
                    : "stream overlay closed");
    }

    if (!visible)
        keyboard_release_guard_ = true;
}

void StreamView::ActivateStreamOverlayAction(
    std::chrono::steady_clock::time_point now)
{
    switch (opennow::input::SelectedOverlayAction(stream_overlay_selection_))
    {
        case opennow::input::OverlayAction::ToggleStatistics:
        {
            stats_overlay_enabled_ = !stats_overlay_enabled_;
            auto settings = opennow::LoadStreamSettings();
            settings.stats_overlay_enabled = stats_overlay_enabled_;
            (void)opennow::SaveStreamSettings(settings);
            stream_overlay_status_ = stats_overlay_enabled_
                ? opennow::Tr("Performance statistics enabled")
                : opennow::Tr("Performance statistics disabled");
            stream_overlay_status_until_ = now + std::chrono::seconds(3);
            if (session_)
                session_->record_ui_event(
                    stats_overlay_enabled_ ? "overlay statistics enabled"
                                           : "overlay statistics disabled");
            break;
        }
        case opennow::input::OverlayAction::RefreshNetwork:
            RefreshNetworkInfo(now);
            stream_overlay_status_ = opennow::Tr("Network information refreshed");
            stream_overlay_status_until_ = now + std::chrono::seconds(3);
            if (session_)
                session_->record_ui_event("overlay network information refreshed");
            break;
        case opennow::input::OverlayAction::Close:
            SetStreamOverlayVisible(false);
            break;
        case opennow::input::OverlayAction::ExitStream:
            ExitStream();
            break;
    }
}

void StreamView::DrawControllerNotice(
    NVGcontext* vg, float x, float y, float width,
    std::chrono::steady_clock::time_point now)
{
    UpdateControllerNotice(now);
    if (controller_notice_text_.empty())
        return;

    const float box_width = std::min(360.0f, width - 36.0f);
    const float box_height = 54.0f;
    const float box_x = x + (width - box_width) * 0.5f;
    const float box_y = y + 18.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, box_width, box_height, 13.0f);
    nvgFillColor(vg, nvgRGBA(7, 11, 14, 232));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 6.0f, box_height, 3.0f);
    nvgFillColor(vg, nvgRGB(55, 220, 125));
    nvgFill(vg);

    nvgFontSize(vg, 21.0f);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFillColor(vg, nvgRGB(248, 252, 249));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(
        vg, box_x + box_width * 0.5f, box_y + box_height * 0.5f,
        controller_notice_text_.c_str(), nullptr);
}

void StreamView::RefreshNetworkInfo(std::chrono::steady_clock::time_point now)
{
    const bool was_2_4_ghz =
        opennow::network::ShouldWarnForStreaming(network_info_.wifi_band);
    if (network_monitor_) {
        const auto snapshot=network_monitor_->Read();
        if(snapshot.valid) network_info_=snapshot.value;
        network_monitor_->Request();
    }
    last_network_info_check_at_ = now;

    const bool is_2_4_ghz =
        opennow::network::ShouldWarnForStreaming(network_info_.wifi_band);
    if (is_2_4_ghz && !was_2_4_ghz) {
        network_warning_visible_until_ = now + std::chrono::seconds(12);
        if (session_)
            session_->record_ui_event("network_warning wifiBand=2.4GHz");
    }
}

void StreamView::DrawNetworkWarning(
    NVGcontext* vg, float x, float y, float width, float height,
    std::chrono::steady_clock::time_point now)
{
    if (stream_overlay_visible_ || now >= network_warning_visible_until_ ||
        !opennow::network::ShouldWarnForStreaming(network_info_.wifi_band))
        return;

    const float box_width = std::min(650.0f, width - 36.0f);
    const float box_height = 72.0f;
    const float box_x = x + (width - box_width) * 0.5f;
    const float box_y = y + height - box_height - 76.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, box_width, box_height, 13.0f);
    nvgFillColor(vg, nvgRGBA(13, 16, 18, 238));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 184, 58, 105));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 6.0f, box_height, 3.0f);
    nvgFillColor(vg, nvgRGB(255, 184, 58));
    nvgFill(vg);

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 18.0f);
    nvgFillColor(vg, nvgRGB(252, 244, 224));
    nvgText(vg, box_x + 22.0f, box_y + 25.0f,
            opennow::Tr("2.4 GHz Wi-Fi may cause poor streaming performance").c_str(), nullptr);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, nvgRGB(190, 194, 196));
    nvgText(vg, box_x + 22.0f, box_y + 49.0f,
            opennow::Tr("For lower latency and fewer frame drops, use 5 GHz Wi-Fi or Ethernet.").c_str(), nullptr);
}

void StreamView::DrawStreamOverlay(
    NVGcontext* vg, float x, float y, float width, float height,
    std::chrono::steady_clock::time_point now)
{
    if (!stream_overlay_visible_)
        return;

    const VideoPerformanceCounters counters = previous_video_counters_;
    const StreamTransportHealth health =
        session_ ? session_->get_transport_health() : StreamTransportHealth {};
    const StreamNetworkCounters network = network_counters_;
    const bool wifi_warning =
        opennow::network::ShouldWarnForStreaming(network_info_.wifi_band);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(2, 5, 7, 190));
    nvgFill(vg);

    const float margin_x = std::clamp(width * 0.0265f, 22.0f, 40.0f);
    const float margin_y = std::clamp(height * 0.033f, 18.0f, 30.0f);
    const float panel_x = x + margin_x;
    const float panel_y = y + margin_y;
    const float panel_width = width - margin_x * 2.0f;
    const float panel_height = height - margin_y * 2.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, panel_x, panel_y, panel_width, panel_height, 18.0f);
    nvgFillColor(vg, nvgRGBA(12, 16, 19, 248));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, nvgRGBA(103, 118, 124, 82));
    nvgStroke(vg);

    const float padding = 30.0f;
    const float header_bottom = panel_y + 105.0f;
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, nvgRGB(77, 218, 130));
    nvgText(vg, panel_x + padding, panel_y + 23.0f, opennow::Tr("STREAM STATUS").c_str(), nullptr);
    nvgFontSize(vg, 25.0f);
    nvgFillColor(vg, nvgRGB(244, 248, 246));
    nvgText(vg, panel_x + padding, panel_y + 54.0f,
            game_title_.empty() ? opennow::Tr("GeForce NOW session").c_str() : game_title_.c_str(), nullptr);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, nvgRGB(142, 153, 160));
    const std::string provider =
        auth_.provider.display_name.empty() ? "GeForce NOW" : auth_.provider.display_name;
    nvgText(vg, panel_x + padding, panel_y + 80.0f,
            (provider + opennow::Tr(" cloud session")).c_str(), nullptr);

    const std::string connection_status = opennow::Tr(
        health.input_ready && counters.decoded_frames > 0 ? "CONNECTED" : "CONNECTING");
    const float status_width = 150.0f;
    const float status_x = panel_x + panel_width - padding - status_width;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, status_x, panel_y + 24.0f, status_width, 30.0f, 15.0f);
    nvgFillColor(vg, health.peer_completed
        ? nvgRGBA(77, 218, 130, 25)
        : nvgRGBA(255, 184, 58, 25));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, health.peer_completed
        ? nvgRGBA(77, 218, 130, 95)
        : nvgRGBA(255, 184, 58, 95));
    nvgStroke(vg);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, health.peer_completed
        ? nvgRGB(114, 232, 157)
        : nvgRGB(255, 198, 91));
    nvgText(vg, status_x + status_width * 0.5f, panel_y + 39.0f,
            connection_status.c_str(), nullptr);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(151, 161, 166));
    nvgText(vg, panel_x + panel_width - padding, panel_y + 79.0f,
            opennow::Tr("B  Close menu").c_str(), nullptr);

    nvgBeginPath(vg);
    nvgRect(vg, panel_x + padding, header_bottom, panel_width - padding * 2.0f, 1.0f);
    nvgFillColor(vg, nvgRGBA(151, 164, 170, 35));
    nvgFill(vg);

    const float content_top = header_bottom + 24.0f;
    const float divider_x = panel_x + panel_width * 0.625f;
    const float left_x = panel_x + padding;
    const float left_width = divider_x - left_x - 26.0f;
    const float right_x = divider_x + 28.0f;
    const float right_edge = panel_x + panel_width - padding;

    nvgBeginPath(vg);
    nvgRect(vg, divider_x, content_top, 1.0f,
            panel_y + panel_height - padding - content_top);
    nvgFillColor(vg, nvgRGBA(151, 164, 170, 35));
    nvgFill(vg);

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(172, 183, 187));
    const std::string live_performance = opennow::Tr("LIVE PERFORMANCE");
    nvgText(vg, left_x, content_top, live_performance.c_str(), nullptr);

    const float metric_top = content_top + 20.0f;
    const float metric_gap = 8.0f;
    const float metric_width = (left_width - metric_gap * 3.0f) * 0.25f;
    const float metric_height = 76.0f;
    auto draw_metric = [vg, metric_top, metric_width, metric_height](
                           float metric_x, const char* label,
                           const std::string& value, bool caution) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, metric_x, metric_top, metric_width, metric_height, 10.0f);
        nvgFillColor(vg, nvgRGBA(24, 30, 33, 225));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, caution
            ? nvgRGBA(255, 184, 58, 80)
            : nvgRGBA(151, 164, 170, 28));
        nvgStroke(vg);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, nvgRGB(132, 144, 150));
        nvgText(vg, metric_x + 13.0f, metric_top + 21.0f, label, nullptr);
        nvgFontSize(vg, 23.0f);
        nvgFillColor(vg, caution ? nvgRGB(255, 198, 91) : nvgRGB(240, 246, 242));
        nvgText(vg, metric_x + 13.0f, metric_top + 51.0f, value.c_str(), nullptr);
    };

    char number[96];
    std::snprintf(number, sizeof(number), "%.0f", presented_fps_);
    draw_metric(left_x, opennow::Tr("DISPLAY FPS").c_str(), number, false);
    std::snprintf(number, sizeof(number), "%.1f Mbps", stream_bitrate_mbps_);
    draw_metric(left_x + (metric_width + metric_gap), opennow::Tr("BITRATE").c_str(), number, false);
    const std::string ping =
        network_rtt_ms_ >= 0 ? std::to_string(network_rtt_ms_) + " ms" : "-- ms";
    draw_metric(left_x + (metric_width + metric_gap) * 2.0f, opennow::Tr("PING").c_str(), ping,
                network_rtt_ms_ >= 80);

    const std::uint64_t packet_total =
        static_cast<std::uint64_t>(network.packets_received) + network.sequence_gaps;
    const double packet_loss = packet_total > 0
        ? static_cast<double>(network.sequence_gaps) * 100.0 /
              static_cast<double>(packet_total)
        : 0.0;
    std::snprintf(number, sizeof(number), "%.2f%%", packet_loss);
    draw_metric(left_x + (metric_width + metric_gap) * 3.0f, opennow::Tr("PACKET LOSS").c_str(), number,
                packet_loss >= 1.0);

    const float details_title_y = metric_top + metric_height + 24.0f;
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(172, 183, 187));
    const std::string stream_details = opennow::Tr("STREAM DETAILS");
    nvgText(vg, left_x, details_title_y, stream_details.c_str(), nullptr);

    const float details_top = details_title_y + 20.0f;
    const float detail_gap = 26.0f;
    const float detail_width = (left_width - detail_gap) * 0.5f;
    auto draw_detail = [vg, details_top, detail_width](
                           float column_x, int row, const std::string& label,
                           const std::string& value, bool caution = false) {
        const float row_y = details_top + row * 36.0f;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 13.0f);
        nvgFillColor(vg, nvgRGB(132, 144, 150));
        nvgText(vg, column_x, row_y + 13.0f, label.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, caution ? nvgRGB(255, 198, 91) : nvgRGB(225, 232, 228));
        nvgText(vg, column_x + detail_width, row_y + 13.0f, value.c_str(), nullptr);
        nvgBeginPath(vg);
        nvgRect(vg, column_x, row_y + 30.0f, detail_width, 1.0f);
        nvgFillColor(vg, nvgRGBA(151, 164, 170, 24));
        nvgFill(vg);
    };

    const std::string resolution = session_
        ? std::to_string(session_->stream_width()) + " × " +
              std::to_string(session_->stream_height())
        : "--";
    draw_detail(left_x, 0, opennow::Tr("Resolution"), resolution);
    std::snprintf(number, sizeof(number), "%.0f / %.0f / %.0f",
                  incoming_fps_, decoded_fps_, presented_fps_);
    draw_detail(left_x, 1, opennow::Tr("FPS  in / decode / display"), number);
    draw_detail(left_x, 2, opennow::Tr("Decode latency p95"),
                std::to_string(counters.decode_us_p95 / 1000) + " ms");
    draw_detail(left_x, 3, opennow::Tr("Render / arrival p95"),
                std::to_string(counters.render_us_p95 / 1000) + " / " +
                    std::to_string(counters.arrival_p95_us / 1000) + " ms");
    draw_detail(left_x, 4, opennow::Tr("Decoder queue"),
                std::to_string(counters.decode_queue_size) + " / " +
                    std::to_string(counters.decode_queue_high_water) + opennow::Tr(" high"));
    draw_detail(left_x, 5, opennow::Tr("Codec / server"),
                stream_codec_ + "  /  " + opennow::routing::ShortHost(actual_server_));

    const float second_column_x = left_x + detail_width + detail_gap;
    std::string network_connection = "Unknown";
    if (network_info_.type == opennow::NetworkConnectionType::Ethernet)
        network_connection = "Ethernet";
    else if (network_info_.type == opennow::NetworkConnectionType::Wifi)
        network_connection = opennow::network::WifiBandLabel(network_info_.wifi_band);
    draw_detail(second_column_x, 0, opennow::Tr("Network"), opennow::Tr(network_connection), wifi_warning);
    std::snprintf(number, sizeof(number), opennow::Tr("%.2f%%  ·  %u late").c_str(),
                  packet_loss, network.late_packets_dropped);
    draw_detail(second_column_x, 1, opennow::Tr("Packet loss"), number, packet_loss >= 1.0);
    draw_detail(second_column_x, 2, opennow::Tr("Incomplete: total / new"),
                std::to_string(network.access_units_dropped) + " / +" +
                    std::to_string(recent_video_drops_), recent_video_drops_ > 0);
    draw_detail(second_column_x, 3, opennow::Tr("NACK recovery requests"),
                std::to_string(network.nack_requests));
    draw_detail(second_column_x, 4, opennow::Tr("Video decoder"),
                session_ ? session_->video_decoder_label() : "--");
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - stream_started_at_).count();
    std::snprintf(number, sizeof(number), "%lld:%02lld",
                  static_cast<long long>(elapsed / 60),
                  static_cast<long long>(elapsed % 60));
    draw_detail(second_column_x, 5, opennow::Tr("Session time"), number);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(172, 183, 187));
    const auto assessment = opennow::TrFormat("Current: {0}",
        {opennow::Tr(opennow::status::Label(performance_assessment_.current))});
    nvgTextBox(vg, left_x, details_top + 242.0f, left_width, assessment.c_str(), nullptr);
    const std::string route = opennow::Tr("Requested server") + ": " +
        opennow::routing::ShortHost(stream_region_) + "  ->  " +
        opennow::routing::ShortHost(actual_server_) +
        (server_redirects_ ? "  (" + opennow::Tr("Server reassigned") + ")" : "");
    nvgFontSize(vg, 12.0f);
    nvgTextBox(vg, left_x, details_top + 267.0f, left_width, route.c_str(), nullptr);
    nvgFontSize(vg, 13.0f);
    const auto recording = opennow::TrFormat("Samples: {0}  |  Incidents: {1}", {
        std::to_string(performance_recorder_.samples()), std::to_string(performance_recorder_.incidents())});
    nvgText(vg, left_x, details_top + 292.0f, recording.c_str(), nullptr);

    float right_y = content_top;
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(172, 183, 187));
    const std::string quick_actions = opennow::Tr("QUICK ACTIONS");
    nvgText(vg, right_x, right_y, quick_actions.c_str(), nullptr);
    right_y += 27.0f;

    if (wifi_warning) {
        const float warning_height = 62.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, right_x, right_y, right_edge - right_x,
                       warning_height, 10.0f);
        nvgFillColor(vg, nvgRGBA(255, 184, 58, 16));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 184, 58, 75));
        nvgStroke(vg);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, nvgRGB(255, 205, 108));
        nvgText(vg, right_x + 14.0f, right_y + 20.0f,
                opennow::Tr("2.4 GHz Wi-Fi detected").c_str(), nullptr);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, nvgRGB(190, 194, 196));
        nvgText(vg, right_x + 14.0f, right_y + 43.0f,
                opennow::Tr("Use 5 GHz or Ethernet for smoother streaming.").c_str(), nullptr);
        right_y += warning_height + 13.0f;
    }

    const std::array<std::string, opennow::input::kOverlayActionCount> action_labels = {
        stats_overlay_enabled_
            ? opennow::Tr("Hide performance statistics")
            : opennow::Tr("Show performance statistics"),
        opennow::Tr("Refresh network information"),
        opennow::Tr("Close menu"),
        opennow::Tr("Exit stream"),
    };
    for (std::size_t index = 0; index < action_labels.size(); ++index)
    {
        const bool selected = index == stream_overlay_selection_;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, right_x, right_y, right_edge - right_x, 34.0f, 8.0f);
        nvgFillColor(vg, selected
            ? nvgRGBA(77, 218, 130, 30)
            : nvgRGBA(27, 33, 37, 220));
        nvgFill(vg);
        nvgStrokeWidth(vg, selected ? 1.5f : 1.0f);
        nvgStrokeColor(vg, selected
            ? nvgRGBA(77, 218, 130, 150)
            : nvgRGBA(140, 154, 161, 38));
        nvgStroke(vg);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 13.0f);
        nvgFillColor(vg, selected
            ? nvgRGB(128, 238, 168)
            : nvgRGB(220, 228, 224));
        const std::string row = selected
            ? "A   " + action_labels[index]
            : "    " + action_labels[index];
        nvgText(vg, right_x + 13.0f, right_y + 17.0f, row.c_str(), nullptr);
        right_y += 40.0f;
    }

    if (!stream_overlay_status_.empty() && now < stream_overlay_status_until_)
    {
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, nvgRGB(114, 232, 157));
        nvgText(vg, right_x, right_y + 7.0f,
                stream_overlay_status_.c_str(), nullptr);
    }
    right_y += 24.0f;

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 13.0f);
    nvgFillColor(vg, nvgRGB(172, 183, 187));
    const std::string shortcuts = opennow::Tr("CONTROLLER SHORTCUTS");
    nvgText(vg, right_x, right_y, shortcuts.c_str(), nullptr);
    right_y += 23.0f;

    auto draw_shortcut = [vg, right_x](
                             float& row_y, const char* keys, const char* action,
                             bool emphasized = false) {
        const float key_width = 132.0f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, right_x, row_y, key_width, 30.0f, 7.0f);
        nvgFillColor(vg, emphasized
            ? nvgRGBA(77, 218, 130, 22)
            : nvgRGBA(31, 38, 42, 230));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, emphasized
            ? nvgRGBA(77, 218, 130, 85)
            : nvgRGBA(140, 154, 161, 65));
        nvgStroke(vg);
        nvgFontSize(vg, 12.0f);
        nvgFillColor(vg, emphasized
            ? nvgRGB(122, 235, 164)
            : nvgRGB(231, 237, 233));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, right_x + key_width * 0.5f, row_y + 15.0f, keys, nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(vg, 13.0f);
        nvgFillColor(vg, nvgRGB(188, 198, 202));
        nvgText(vg, right_x + key_width + 15.0f, row_y + 15.0f, action, nullptr);
        row_y += 36.0f;
    };

    draw_shortcut(right_y, "MINUS  +  PLUS", opennow::Tr("Open or close menu").c_str(), true);
    draw_shortcut(right_y, "MINUS + R3", opennow::Tr("Hold 0.3s; release allowed").c_str());
    draw_shortcut(right_y, "ZL / L / R / ZR", "Esc / Tab / Alt+Tab / Win");
    draw_shortcut(right_y, AutoLoginAvailable()?"Y / X":"Y",
        opennow::Tr(AutoLoginAvailable()?"Keyboard / auto-login":"On-screen keyboard").c_str());
    draw_shortcut(right_y, "ZL + ZR + MINUS", opennow::Tr("Exit stream").c_str());

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 12.0f);
    nvgFillColor(vg, nvgRGB(103, 116, 122));
    nvgText(vg, right_x, panel_y + panel_height - 26.0f,
            opennow::Tr("Menu controls stay on your Switch and are not sent to the game.").c_str(), nullptr);
}

void StreamView::DrawNteAutoLoginStatus(
    NVGcontext* vg, float x, float y, float width, float height,
    std::chrono::steady_clock::time_point now) {
    const bool command_mode=shortcut_router_.Armed();
    if(auto_login_choice_state_.Active())return;
    if (!command_mode && ((!is_nte_session_ && !game_login_profile_ && !EpicAutoLoginAvailable()) || now >= nte_status_until_))
        return;

    std::string text = command_mode ? "ZL Esc  /  L Tab  /  R Alt+Tab  /  ZR Win  /  Y " +
        opennow::Tr("Keyboard") + "  |  B " + opennow::Tr("Cancel") : opennow::Tr(nte_status_);
    if(command_mode) {
        if(AutoLoginAvailable())text+="  |  X "+opennow::Tr("Auto-login");
        const auto remaining=shortcut_router_.RemainingMs(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        if(remaining>=0)text+="  |  "+std::to_string((remaining+999)/1000)+" s";
    }
    if (text.empty()) {
        if (game_login_profile_)
            text = std::string(game_login_profile_->folder) + ": " + opennow::Tr("Auto-login: Minus + R3, then X");
        else text = nte_credentials_.valid()
            ? opennow::Tr("NTE: Minus + R3, then X")
            : opennow::Tr("Configure NTE Auto-login on the game page");
    }
    if (AutoLoginActive())
        text += "  |  B "+opennow::Tr("Cancel");

    const float box_width = std::min(command_mode?900.0f:560.0f, width - 36.0f);
    const float box_height = 46.0f;
    const float box_x = x + 18.0f;
    const float box_y = y + height - box_height - 18.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, box_width, box_height, 11.0f);
    nvgFillColor(vg, nvgRGBA(7, 12, 14, 220));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 6.0f, box_height, 3.0f);
    nvgFillColor(vg, nvgRGB(55, 220, 125));
    nvgFill(vg);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, command_mode?16.0f:19.0f);
    if(command_mode||epic_trace_) {
        float bounds[4]{};nvgTextBounds(vg,0,0,text.c_str(),nullptr,bounds);
        const float measured=bounds[2]-bounds[0];
        if(measured>box_width-40)nvgFontSize(vg,std::max(12.0f,(command_mode?16.0f:19.0f)*(box_width-40)/measured));
    }
    nvgFillColor(vg, nvgRGB(246, 250, 248));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, box_x + 20.0f, box_y + box_height * 0.5f, text.c_str(), nullptr);
}

void StreamView::UpdateSessionLimitNotice(std::chrono::steady_clock::time_point now) {
    if (!free_tier_session_ || stream_started_at_.time_since_epoch().count() == 0)
        return;

    constexpr auto kFreeSessionLimit = std::chrono::hours(1);
    const auto elapsed = now - stream_started_at_;
    const auto remaining = elapsed < kFreeSessionLimit
        ? std::chrono::duration_cast<std::chrono::seconds>(kFreeSessionLimit - elapsed)
        : std::chrono::seconds(0);

    if (elapsed >= kFreeSessionLimit ||
        (session_ && session_->is_terminal() && elapsed >= std::chrono::minutes(59))) {
        BeginStreamEnd(opennow::StreamEndReason::FreeSessionEnded, now);
        return;
    }

    if (!one_minute_warning_shown_ && remaining <= std::chrono::seconds(60)) {
        one_minute_warning_shown_ = true;
        five_minute_warning_shown_ = true;
        session_limit_notice_ = SessionLimitNotice::OneMinute;
        session_notice_visible_until_ = now + std::chrono::seconds(10);
        if (session_)
            session_->record_ui_event("free_session_warning remainingSeconds=60");
    } else if (!five_minute_warning_shown_ && remaining <= std::chrono::minutes(5)) {
        five_minute_warning_shown_ = true;
        session_limit_notice_ = SessionLimitNotice::FiveMinutes;
        session_notice_visible_until_ = now + std::chrono::seconds(8);
        if (session_)
            session_->record_ui_event("free_session_warning remainingSeconds=300");
    }
}

void StreamView::BeginStreamEnd(
    opennow::StreamEndReason reason,
    std::chrono::steady_clock::time_point now) {
    if (reason == opennow::StreamEndReason::None ||
        stream_end_reason_ != opennow::StreamEndReason::None)
        return;

    stream_end_reason_ = reason;
    stream_end_return_ready_ = false;
    stream_end_started_at_ = now;
    stream_auto_exit_at_ = now + std::chrono::seconds(15);
    if (reason == opennow::StreamEndReason::FreeSessionEnded) {
        session_limit_ended_ = true;
        session_limit_notice_ = SessionLimitNotice::Ended;
    }

    std::string event = "stream_end reason=";
    switch (reason) {
        case opennow::StreamEndReason::FreeSessionEnded:
            event += "free_session_limit";
            break;
        case opennow::StreamEndReason::NetworkLost:
            event += "network_lost";
            break;
        case opennow::StreamEndReason::ServerDisconnected:
            event += "server_disconnected";
            break;
        case opennow::StreamEndReason::StartupTimedOut:
        case opennow::StreamEndReason::ConnectionFailed:
            event += "connection_failed";
            break;
        case opennow::StreamEndReason::StreamEnded:
            event += "server_ended_stream";
            break;
        case opennow::StreamEndReason::VideoTimedOut:
            event += "video_timeout";
            break;
        case opennow::StreamEndReason::None:
            return;
    }
    event += " autoExitSeconds=15";
    if (session_) {
        session_->record_ui_event(event);
        // Let transport and decoder workers finish during the 15-second
        // notice. Destruction then cannot leave Borealis input blocked.
        session_->request_stop();
    }
}

void StreamView::UpdateStreamEndState(std::chrono::steady_clock::time_point now) {
    if (!session_ || stream_end_reason_ != opennow::StreamEndReason::None)
        return;

    if (last_network_info_check_at_.time_since_epoch().count() == 0 ||
        now - last_network_info_check_at_ >= std::chrono::seconds(30)) {
        RefreshNetworkInfo(now);
    }

    if (last_network_check_at_.time_since_epoch().count() == 0 ||
        now - last_network_check_at_ >= std::chrono::seconds(1)) {
        const auto snapshot=network_monitor_
            ? network_monitor_->Read()
            : opennow::BackgroundSampler<opennow::NetworkConnectionInfo>::Snapshot{};
        // Missing/stale service state is unknown, not proof of a disconnect.
        const bool reported = !snapshot.valid || now-snapshot.updated>std::chrono::seconds(5)
            ? true : snapshot.value.connected;
        if(snapshot.valid) {
            const bool was_2_4=opennow::network::ShouldWarnForStreaming(network_info_.wifi_band);
            network_info_=snapshot.value;
            if(!was_2_4 && opennow::network::ShouldWarnForStreaming(network_info_.wifi_band))
                network_warning_visible_until_=now+std::chrono::seconds(12);
        }
        if (reported != internet_connected_)
            session_->record_ui_event(std::string("network_status connected=") + (reported ? "1" : "0"));
        internet_connected_ = reported;
        last_network_check_at_ = now;
    }

    const StreamTransportHealth health = session_->get_transport_health();
    stream_established_ = stream_established_ ||
        (health.video_started && session_->get_video_performance().decoded_frames > 0);
    recovery_budget_.Observe(health.video_started &&
        health.video_idle < std::chrono::milliseconds(500) &&
        health.peer_terminal == opennow::PeerTerminalKind::None, now);
    // Incoming video is stronger evidence than the OS connectivity label.
    // Re-arm only when delivery has actually stopped; a transient NIFM status
    // must never stop a live transport/decoder pair.
    if (internet_connected_ || (health.video_started && health.video_idle < std::chrono::milliseconds(500)))
        network_unavailable_since_ = {};
    else if (network_unavailable_since_.time_since_epoch().count() == 0)
        network_unavailable_since_ = now;
    opennow::StreamEndSignals signals;
    signals.free_tier = free_tier_session_;
    signals.startup_timed_out = health.startup_timed_out;
    signals.startup_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - initial_transport_started_at_);
    signals.session_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - stream_started_at_);
    signals.internet_connected = internet_connected_;
    signals.peer_completed = health.peer_completed;
    signals.peer_terminal = health.peer_terminal;
    signals.signaling_connected = health.signaling_connected;
    signals.video_started = health.video_started;
    signals.video_decoded = stream_established_;
    signals.video_idle = health.video_idle;
    if (network_unavailable_since_.time_since_epoch().count() != 0)
        signals.network_unavailable = std::chrono::duration_cast<std::chrono::milliseconds>(now - network_unavailable_since_);
    const auto reason = opennow::DetectStreamEnd(signals);
    // A transient ICE disconnect is allowed to heal without replacing the rig
    // connection. A terminal peer with no video for three seconds is real loss.
    if (opennow::recovery::Eligible(reason, stream_established_) &&
        health.video_started && health.video_idle < std::chrono::seconds(3))
        return;
    if (reason != opennow::StreamEndReason::None)
        session_->record_ui_event("stream_end_evidence internet=" + std::to_string(signals.internet_connected) +
            " firstPictureTimeout=" + std::to_string(reason == opennow::StreamEndReason::StartupTimedOut) +
            " networkUnavailableMs=" + std::to_string(signals.network_unavailable.count()) +
            " videoIdleMs=" + std::to_string(signals.video_idle.count()) +
            " peerTerminal=" + std::to_string(static_cast<int>(signals.peer_terminal)) +
            " signaling=" + std::to_string(signals.signaling_connected));
    if (opennow::recovery::Eligible(reason, stream_established_)) {
        StartRecovery(now);
        return;
    }
    if (opennow::recovery::OfferManualStartupRetry(reason, stream_established_)) {
        // Initial signaling/ICE failure needs an explicit choice, not a DELETE
        // of the cloud game after a notice countdown. Reuse the bounded manual
        // recovery screen; only A starts another transport to this session.
        recovering_ = true;
        recovery_failed_ = true;
        recovery_terminal_ = false;
        recovery_input_ready_ = false;
        preserve_cloud_session_ = true;
        recovery_error_ = "Could not reconnect to the streaming server";
        session_->request_stop();
        return;
    }
    BeginStreamEnd(reason, now);
}

void StreamView::DrawStreamEndNotice(
    NVGcontext* vg, float x, float y, float width,
    std::chrono::steady_clock::time_point now) {
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        return;

    std::string title;
    std::string detail;
    switch (stream_end_reason_) {
        case opennow::StreamEndReason::FreeSessionEnded:
            title = "Free session ended";
            detail = "The one-hour GeForce NOW limit was reached";
            break;
        case opennow::StreamEndReason::NetworkLost:
            title = "Network connection lost";
            detail = "Check the Switch internet connection";
            break;
        case opennow::StreamEndReason::ServerDisconnected:
            title = "Streaming server disconnected";
            detail = "The connection to the GeForce NOW rig was lost";
            break;
        case opennow::StreamEndReason::StartupTimedOut:
        case opennow::StreamEndReason::ConnectionFailed:
            title = "Stream connection failed";
            detail = "The GeForce NOW rig could not be reached";
            break;
        case opennow::StreamEndReason::StreamEnded:
            title = "Stream ended by server";
            detail = "GeForce NOW closed this streaming session";
            break;
        case opennow::StreamEndReason::VideoTimedOut:
            title = "Video stream timed out";
            detail = "No video packets were received for 15 seconds";
            break;
        case opennow::StreamEndReason::None:
            return;
    }

    const auto remaining_ms = std::max<std::int64_t>(0,
        std::chrono::duration_cast<std::chrono::milliseconds>(stream_auto_exit_at_ - now).count());
    const int remaining_seconds = static_cast<int>((remaining_ms + 999) / 1000);
    title=opennow::Tr(title);
    detail=opennow::Tr(detail)+"  |  "+opennow::TrFormat("Returning in {0}s",{std::to_string(remaining_seconds)});

    const float box_width = std::min(560.0f, width - 36.0f);
    const bool can_return = stream_end_reason_ == opennow::StreamEndReason::StreamEnded;
    const float box_height = can_return ? 108.0f : 82.0f;
    const float box_x = x + width - box_width - 18.0f;
    const float box_y = y + 16.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, box_width, box_height, 13.0f);
    nvgFillColor(vg, nvgRGBA(8, 11, 14, 232));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 7.0f, box_height, 3.5f);
    nvgFillColor(vg, nvgRGB(255, 91, 91));
    nvgFill(vg);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 23.0f);
    nvgFillColor(vg, nvgRGB(252, 246, 246));
    nvgText(vg, box_x + 22.0f, box_y + 29.0f, title.c_str(), nullptr);
    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, nvgRGB(188, 195, 204));
    nvgText(vg, box_x + 22.0f, box_y + 59.0f, detail.c_str(), nullptr);
    if (can_return) {
        const auto hint = opennow::Tr("A / B: Return to menu");
        nvgFillColor(vg, nvgRGB(223, 229, 236));
        nvgText(vg, box_x + 22.0f, box_y + 86.0f, hint.c_str(), nullptr);
    }
}

void StreamView::DrawSessionLimitNotice(
    NVGcontext* vg, float x, float y, float width,
    std::chrono::steady_clock::time_point now) {
    if (!free_tier_session_ || session_limit_notice_ == SessionLimitNotice::None)
        return;

    constexpr auto kFreeSessionLimit = std::chrono::hours(1);
    const auto elapsed = now - stream_started_at_;
    const int remaining_seconds = static_cast<int>(std::max<int64_t>(0,
        std::chrono::duration_cast<std::chrono::seconds>(kFreeSessionLimit - elapsed).count()));
    const bool prominent = session_limit_notice_ == SessionLimitNotice::Ended ||
                           now < session_notice_visible_until_;
    if (session_limit_notice_ == SessionLimitNotice::FiveMinutes && !prominent)
        return;

    std::string text;
    NVGcolor accent = nvgRGB(255, 184, 58);
    if (session_limit_notice_ == SessionLimitNotice::Ended) {
        text = "Free session ended";
        accent = nvgRGB(255, 91, 91);
    } else if (session_limit_notice_ == SessionLimitNotice::FiveMinutes) {
        text = "Free session: 5 minutes remaining";
    } else {
        text = prominent
            ? "Free session ends in 60 seconds"
            : "Session 0:" + (remaining_seconds < 10 ? std::string("0") : std::string()) +
                std::to_string(remaining_seconds);
        accent = nvgRGB(255, 112, 82);
    }

    text=opennow::TrMessage(text);
    const float box_width = prominent ? std::min(390.0f, width - 36.0f) : 190.0f;
    const float box_height = prominent ? 50.0f : 40.0f;
    const float box_x = x + width - box_width - 18.0f;
    const float box_y = y + 16.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, box_width, box_height, prominent ? 12.0f : 9.0f);
    nvgFillColor(vg, nvgRGBA(7, 10, 12, prominent ? 224 : 198));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box_x, box_y, 6.0f, box_height, 3.0f);
    nvgFillColor(vg, accent);
    nvgFill(vg);

    nvgFontSize(vg, prominent ? 22.0f : 21.0f);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFillColor(vg, nvgRGB(250, 252, 250));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, box_x + box_width * 0.5f, box_y + box_height * 0.5f, text.c_str(), nullptr);
}
