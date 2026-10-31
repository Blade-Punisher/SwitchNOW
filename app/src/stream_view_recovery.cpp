#include "StreamView.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include <algorithm>

void StreamView::StartRecovery(std::chrono::steady_clock::time_point now) {
    if (recovery_job_ || exit_requested_) return;
    CancelKeyboardShortcut();
    if (!recovering_) recovery_input_ready_ = false;
    recovering_ = true;
    preserve_cloud_session_ = true;
    recovery_connect_deadline_ = {};
    if (!recovery_budget_.Take()) {
        recovery_failed_ = true;
        recovery_error_ = "Automatic reconnect attempts exhausted";
        if (session_) session_->request_stop();
        return;
    }
    recovery_failed_ = false;
    recovery_error_.clear();
    recovery_retry_at_ = now;
    HideInlineKeyboard(false);
    CancelNteAutoLogin();
    SendNeutralControllerReports();
    if (session_ && touch_was_down_) session_->send_mouse_left_button(false);
    touch_was_down_ = false;
    ResetControllerDeliveryState();
    stream_overlay_visible_ = false;
    // Job owns its results. No callback captures StreamView or an NVDEC frame.
    auto job = std::make_shared<RecoveryJob>();
    job->auth = auth_;
    recovery_job_ = job;
    const auto client = client_;
    const auto id = cloud_session_id_;
    opennow::LogRuntimeEvent("recovery", "checking", "attempt=" +
        std::to_string(recovery_budget_.attempts()));
    const bool wait_for_network = !internet_connected_;
    const auto health = session_ ? session_->get_transport_health() : StreamTransportHealth {};
    const bool claim_offer = session_ && opennow::recovery::NeedsOfferClaim(
        health.offer_received, health.video_started, health.startup_timed_out, recovery_budget_.attempts());
    opennow::LogRuntimeEvent("recovery", "offer_recovery", "claim=" + std::to_string(claim_offer) +
        " action=" + (claim_offer ? std::string("claim_resume") :
            !health.offer_received && !health.video_started ? std::string("fresh_signaling") :
            std::string("existing_session_recovery")));
    const auto failed_signaling = session_ && !health.offer_received && !health.video_started
        ? session_->signaling_endpoint() : std::string{};
    brls::async([job, client, id, wait_for_network, claim_offer, failed_signaling]() {
        try {
            // An offline Switch can reject HTTP immediately. Give Wi-Fi time
            // to return instead of spending both retries in three seconds.
            const auto network_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (wait_for_network && !job->cancelled.load() &&
                std::chrono::steady_clock::now() < network_deadline &&
                !opennow::NetworkUtils::HasInternetConnection())
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            job->info = client.ReconnectSession(job->auth, id,
                [job] { return job->cancelled.load(std::memory_order_acquire); }, claim_offer, failed_signaling);
        } catch (const opennow::CloudSessionNotFound&) {
            job->terminal = true;
            job->error = "The cloud session is no longer available";
        } catch (const opennow::ReauthenticationRequired&) {
            job->terminal = true;
            job->error = "Sign in again to continue";
        } catch (const std::exception&) {
            job->error = "Could not reconnect to the streaming server";
        } catch (...) {
            job->error = "Could not reconnect to the streaming server";
        }
        job->done.store(true, std::memory_order_release);
    });
}

bool StreamView::DrawRecovery(NVGcontext* vg, float x, float y, float width, float height) {
    if (!recovering_) return false;
    const auto now = std::chrono::steady_clock::now();
    if (session_) session_->poll(); // signaling, ICE and input-channel maintenance
    if (now - last_network_check_at_ >= std::chrono::seconds(1)) {
        internet_connected_ = opennow::NetworkUtils::HasInternetConnection();
        last_network_check_at_ = now;
    }
    if (free_tier_session_ && now - stream_started_at_ >= std::chrono::hours(1)) {
        if (recovery_job_) recovery_job_->cancelled.store(true);
        recovery_job_.reset();
        recovering_ = false;
        preserve_cloud_session_ = false;
        BeginStreamEnd(opennow::StreamEndReason::FreeSessionEnded, now);
        return false;
    }
    const auto health = session_ ? session_->get_transport_health() : StreamTransportHealth {};
    const bool fresh = health.video_started && health.video_idle < std::chrono::milliseconds(500)
        && health.peer_terminal == opennow::PeerTerminalKind::None
        && session_->get_video_performance().decoded_frames > 0;
    if (!recovery_failed_ && fresh) {
        // The old connection may recover while CloudMatch is still answering.
        // Keep it instead of replacing a healthy transport.
        if (recovery_job_) recovery_job_->cancelled.store(true);
        recovery_job_.reset();
        recovering_ = false;
        preserve_cloud_session_ = false;
        recovery_connect_deadline_ = {};
        ResetControllerDeliveryState();
#ifdef __SWITCH__
        { std::lock_guard<std::mutex> lock(controller_sample_mutex_);
          latched_controller_edges_ = {}; }
#endif
        touch_was_down_ = false;
        opennow::LogRuntimeEvent("recovery", "video_restored");
        return false;
    }
    if (recovery_job_ && recovery_job_->done.load(std::memory_order_acquire)) {
        const auto job = recovery_job_;
        recovery_job_.reset();
        if (!job->error.empty()) {
            recovery_error_ = job->error;
            recovery_terminal_ = job->terminal;
            recovery_retry_at_ = now + std::chrono::seconds(3);
            recovery_failed_ = job->terminal || recovery_budget_.attempts() >= 2;
            if (recovery_failed_ && session_) session_->request_stop();
            opennow::LogRuntimeEvent("recovery", "lookup_failed",
                "terminal=" + std::to_string(job->terminal));
        } else {
            // Join all old workers and release frames before starting any new
            // decoder/audio/transport. Reusing two live NVDEC contexts is unsafe.
            if (session_) { session_->stop(); session_.reset(); }
            auth_ = job->auth;
            const auto& info = job->info;
            try {
                const std::string jwt = auth_.tokens.id_token.empty()
                    ? auth_.tokens.access_token : auth_.tokens.id_token;
                session_ = std::make_unique<WebRtcSession>(info.signaling_url, jwt,
                    cloud_session_id_, info.media_ip, info.media_port, info.ice_servers, session_settings_, info.cloud_client_profile);
                session_->start();
                actual_server_ = info.server_ip;
                initial_server_ = info.initial_server_ip; server_redirects_ = info.server_redirects;
                media_endpoint_ = info.media_ip;
                previous_video_counters_ = {};
                network_counters_ = {};
                previous_pause_stats_ = {};
                previous_client_stats_ = {};
                performance_assessment_ = {};
                ++performance_transport_epoch_;
                fps_window_started_ = now;
                network_unavailable_since_ = {};
                ResetControllerDeliveryState();
                touch_was_down_ = touch_pointer_initialized_ = false;
#ifdef __SWITCH__
                { std::lock_guard<std::mutex> lock(controller_sample_mutex_);
                  latched_controller_edges_ = {}; }
#endif
                recovery_connect_deadline_ = now + std::chrono::seconds(25);
                recovery_error_.clear();
                opennow::LogRuntimeEvent("recovery", "connecting");
            } catch (...) {
                if (session_) { session_->stop(); session_.reset(); }
                recovery_error_ = "Could not reconnect to the streaming server";
                recovery_retry_at_ = now + std::chrono::seconds(3);
                recovery_failed_ = recovery_budget_.attempts() >= 2;
            }
        }
    }
    if (!recovery_failed_ && !recovery_job_) {
        if (recovery_connect_deadline_ != std::chrono::steady_clock::time_point{} &&
            (now >= recovery_connect_deadline_ ||
             (session_ && session_->get_transport_health().peer_terminal != opennow::PeerTerminalKind::None))) {
            if (session_) {
                session_->note_recovery_end(now >= recovery_connect_deadline_);
                session_->request_stop();
            }
            recovery_connect_deadline_ = {};
            recovery_retry_at_ = now + std::chrono::seconds(3);
            recovery_terminal_ = session_ && session_->get_transport_health().peer_terminal == opennow::PeerTerminalKind::Closed;
            recovery_failed_ = recovery_terminal_ || recovery_budget_.attempts() >= 2;
            recovery_error_ = recovery_terminal_ ? "The cloud session is no longer available" : "Could not reconnect to the streaming server";
        } else if (recovery_connect_deadline_ == std::chrono::steady_clock::time_point{} && now >= recovery_retry_at_) {
            StartRecovery(now);
        }
    }
    brls::ControllerState state;
    brls::Application::getPlatform()->getInputManager()->updateUnifiedControllerState(&state);
    const bool a = state.buttons[brls::BUTTON_A], b = state.buttons[brls::BUTTON_B],
        end = state.buttons[brls::BUTTON_X];
    if (!a && !b && !end) recovery_input_ready_ = true;
    if (recovery_input_ready_ && b && !recovery_b_down_) { ExitStream(false); return true; }
    if (recovery_input_ready_ && end && !recovery_x_down_) { ExitStream(true); return true; }
    if (recovery_input_ready_ && a && !recovery_a_down_ && recovery_failed_ && !recovery_terminal_) {
        recovery_budget_.ManualRetry();
        StartRecovery(now);
    }
    recovery_a_down_ = a; recovery_b_down_ = b; recovery_x_down_ = end;
    nvgBeginPath(vg); nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(9, 13, 19)); nvgFill(vg);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 30); nvgFillColor(vg, nvgRGB(98, 238, 152));
    const auto title = opennow::Tr(recovery_failed_ ? "Connection recovery paused" : "Restoring connection...");
    nvgText(vg, x + width / 2, y + height / 2 - 60, title.c_str(), nullptr);
    nvgFontSize(vg, 19); nvgFillColor(vg, nvgRGB(223, 229, 236));
    const auto detail = opennow::Tr(recovery_error_.empty()
        ? (recovery_job_ ? (internet_connected_ ? "Checking your existing cloud session" : "Waiting for network connection") : "Waiting for video from the same game")
        : recovery_error_);
    nvgText(vg, x + width / 2, y + height / 2, detail.c_str(), nullptr);
    nvgFontSize(vg, 16);
    const auto buttons = opennow::Tr(recovery_failed_ && !recovery_terminal_
        ? "A: Retry   B: Menu (keep game)   X: End game"
        : "B: Menu (keep game)   X: End game");
    nvgText(vg, x + width / 2, y + height / 2 + 65, buttons.c_str(), nullptr);
    return true;
}
