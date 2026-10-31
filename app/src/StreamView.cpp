#include "StreamView.hpp"
#include "home_artwork_toast.hpp"
#include "input/TouchMapping.hpp"
#include "membership_tier_policy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "stream_diagnostics.hpp"
#include "stream_settings.hpp"
#include "diagnostic_report.hpp"
#include "app_paths.hpp"
#include "routing_diagnostics.hpp"
#include "localization.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

namespace
{

} // namespace

StreamView::StreamView(
    const std::string& signaling_url,
    const std::string& jwt_token,
    const std::string& session_id,
    const std::string& media_ip,
    int media_port,
    const std::vector<opennow::IceServerInfo>& ice_servers,
    const opennow::GfnClient& client,
    const opennow::AuthSession& auth,
    const std::string& game_title,
    const std::string& requested_region, const std::string& actual_server, bool resumed,
    const std::string& initial_server, unsigned redirects, opennow::gfn::cloud_session::ClientProfile profile,
    const std::string& launch_store)
    : client_(client)
    , auth_(auth)
    , cloud_session_id_(session_id)
    , game_title_(game_title) {
    launch_store_=launch_store;
    epic_login_available_=opennow::autologin::epic::Eligible(game_title_,launch_store_);
    auto stream_settings = opennow::LoadStreamSettings();
    if (resumed) {
        const auto saved = client_.LoadResumableCloudSession(auth_);
        if (saved && saved->session_id == session_id &&
            opennow::recovery::ValidFormat(saved->width, saved->height, saved->fps, saved->codec)) {
            stream_settings.width = saved->width;
            stream_settings.height = saved->height;
            stream_settings.fps = saved->fps;
            stream_settings.codec = saved->codec;
        }
    }
    session_settings_ = stream_settings;
    debug_diagnostics_ = stream_settings.debug_diagnostics;
    stats_overlay_enabled_ = stream_settings.stats_overlay_enabled;
    stream_codec_ = stream_settings.codec;
    stream_region_ = requested_region;
    actual_server_ = actual_server;
    initial_server_ = initial_server; server_redirects_ = redirects;
    media_endpoint_ = media_ip;
    controller_layout_ = stream_settings.controller_layout;
    opennow::SetStreamDiagnosticsEnabled(debug_diagnostics_);
    free_tier_session_ = opennow::membership::IsVerifiedFreeTier(
        auth_.user.membership_tier, auth_.user.membership_tier_verified);
    stream_started_at_ = std::chrono::steady_clock::now();
    initial_transport_started_at_ = stream_started_at_;
    stream_started_at_ -= client_.MarkCloudSessionStreaming(auth_, cloud_session_id_, game_title_, actual_server_,launch_store_);
    is_nte_session_ = opennow::IsNevernessToEverness(game_title_);
    if (!is_nte_session_) game_login_profile_ = opennow::autologin::FindProfile(game_title_);
    if (is_nte_session_) {
        nte_credentials_ = opennow::LoadNteCredentials();
        nte_status_until_ = stream_started_at_ + std::chrono::seconds(12);
    }
    if (game_login_profile_) nte_status_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(12);
#ifdef __SWITCH__
    padInitialize(&switch_controller_sources_[0], HidNpadIdType_Handheld);
    for (std::size_t source = 1; source < switch_controller_sources_.size(); ++source)
    {
        padInitialize(
            &switch_controller_sources_[source],
            static_cast<HidNpadIdType>(source - 1));
    }
    swap_half_joycon_stick_to_dpad_ =
        brls::Application::isSwapHalfJoyconStickToDpad();
#endif
    session_ = std::make_unique<WebRtcSession>(
        signaling_url,
        jwt_token,
        session_id,
        media_ip,
        media_port,
        ice_servers, session_settings_, profile);
    session_->start();
    if (resumed) {
        stream_established_ = true;
        preserve_cloud_session_ = true;
        recovering_ = true;
        recovery_budget_.Take();
        recovery_connect_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    }
    try {
    network_monitor_ = std::make_unique<opennow::BackgroundSampler<opennow::NetworkConnectionInfo>>(
        [info = opennow::NetworkConnectionInfo{}, full_at = std::chrono::steady_clock::time_point{}]() mutable {
            const auto now = std::chrono::steady_clock::now();
            if (full_at.time_since_epoch().count()==0 || now-full_at>=std::chrono::seconds(30)) {
                info=opennow::NetworkUtils::GetConnectionInfo(); full_at=now;
            } else info.connected=opennow::NetworkUtils::HasInternetConnection();
            return info;
        }, std::chrono::seconds(1));
        session_->record_ui_event("network_monitor owner=background");
    } catch(...) {
        session_->record_ui_event("network_monitor unavailable; connectivity status unknown");
    }
    RefreshNetworkInfo(stream_started_at_);
    
    // Suggest the view to take all available space
    this->setWidth(brls::Application::windowWidth);
    this->setHeight(brls::Application::windowHeight);
    this->setHideHighlight(true);
    this->setHideHighlightBackground(true);
    this->setHideHighlightBorder(true);
    
    this->setFocusable(true);
#ifdef __SWITCH__
    controller_sampler_running_.store(true, std::memory_order_release);
    controller_sampler_thread_ =
        std::thread(&StreamView::SampleSwitchControllers, this);
#endif
}

StreamView::~StreamView() {
    CancelEpicAutoLogin();
    CancelKeyboardShortcut();
    if (recovery_job_) recovery_job_->cancelled.store(true);
    opennow::SetSensitiveInputLoggingSuppressed(false);
    std::fill(nte_credentials_.password.begin(), nte_credentials_.password.end(), '\0');
    std::fill(nte_text_buffer_.begin(), nte_text_buffer_.end(), '\0');
#ifdef __SWITCH__
    controller_sampler_running_.store(false, std::memory_order_release);
    if (controller_sampler_thread_.joinable())
        controller_sampler_thread_.join();
    if (active_keyboard_view_ == this)
        active_keyboard_view_ = nullptr;
    if (keyboard_launched_) {
        swkbdInlineClose(&inline_keyboard_);
        keyboard_launched_ = false;
    }
#endif
    brls::Application::getPlatform()->disableScreenDimming(false);
    brls::Application::getPlatform()->getInputManager()->setPointerLock(false);
    StopCloudSessionAsync();
    if (session_) {
        const NetworkRttStats network_rtt = session_->get_network_rtt_stats();
        const auto video = session_->get_video_performance();
        const auto net = session_->get_network_counters();
        const auto exit_health = session_->get_transport_health();
        const std::string exit_phase = video.decoded_frames > 0 ? "video" :
            !exit_health.offer_received ? "waiting_offer" : !exit_health.peer_completed ? "ice_dtls" : "first_picture";
        opennow::routing::Event(opennow::AppHomePath(), "stream_result", {
            {"actual_host", opennow::routing::Host(actual_server_)},
            {"media_host", opennow::routing::Host(media_endpoint_)},
            {"rtt_median_ms", std::to_string(network_rtt.median_ms)},
            {"rtt_p95_ms", std::to_string(network_rtt.p95_ms)}, {"rtt_samples", std::to_string(network_rtt.samples)},
            {"rtt_source", std::to_string(static_cast<int>(network_rtt.source))},
            {"frames_in", std::to_string(video.access_units)}, {"displayed", std::to_string(video.presented_frames)},
            {"damaged", std::to_string(net.access_units_dropped)}, {"late", std::to_string(net.late_packets_dropped)}});
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        opennow::report::SaveSessionSummary(opennow::AppHomePath(),
            "session_metrics ended_at_ms=" + std::to_string(stamp) +
            " transport_scope=last_attempt attempt_id=" + std::to_string(session_->diagnostic_attempt_id()) + " phase=" + exit_phase +
            " cloud_session=" + cloud_session_id_ +
            " startup_timeout=" + std::to_string(exit_health.startup_timed_out) +
            " server=" + actual_server_ +
            " initial_server=" + initial_server_ + " server_redirects=" + std::to_string(server_redirects_) +
            " frames_in=" + std::to_string(video.access_units) +
            " decoded=" + std::to_string(video.decoded_frames) +
            " displayed=" + std::to_string(video.presented_frames) +
            " fps_in=" + std::to_string(incoming_fps_) +
            " fps_decode=" + std::to_string(decoded_fps_) +
            " fps_display=" + std::to_string(presented_fps_) +
            " backend=" + session_->video_decoder_label() +
            " damaged=" + std::to_string(net.access_units_dropped) +
            " late=" + std::to_string(net.late_packets_dropped) +
            " ping_median_ms=" + std::to_string(network_rtt.median_ms) +
            " ping_p95_ms=" + std::to_string(network_rtt.p95_ms) +
            " last_window_assessment=" + opennow::status::Label(performance_assessment_.current) + "\n" +
            performance_recorder_.Summary());
        opennow::report::SavePerformance(opennow::AppHomePath(),
            performance_recorder_.TimelineCsv(), performance_recorder_.IncidentsCsv());
        const auto network_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - stream_started_at_).count();
        const auto seconds = std::max<std::uint64_t>(1, performance_recorder_.observed_video_ms() / 1000);
        const auto bounded = [](std::uint64_t value) { return static_cast<int>(std::min<std::uint64_t>(value, 86400000)); };
        using pf = opennow::performance::Field;
        if (network_rtt.samples >= 5 && network_rtt.median_ms > 0 &&
            network_rtt.snapshot_age_ms >= 0 && network_rtt.snapshot_age_ms < 5000 &&
            (network_rtt.source != NetworkRttSource::Stun || network_rtt.stun_sample_age_ms <= 5000) &&
            network_seconds >= 60 && seconds >= 60)
            client_.RecordObservedStreamRtt(
                auth_, stream_region_, actual_server_, media_endpoint_, network_rtt.median_ms,
                network_rtt.p95_ms, bounded(performance_recorder_.Total(pf::TransportPauses)),
                bounded(performance_recorder_.Total(pf::TransportPauseMs)), bounded(seconds),
                bounded(performance_recorder_.Total(pf::PlaybackPauses)),
                bounded(performance_recorder_.Total(pf::PlaybackPauseMs)), initial_server_, cloud_session_id_,
                bounded(performance_recorder_.low_display_ms()),
                bounded(performance_recorder_.Total(pf::AudioUnderruns)));
        session_->stop();
    }
}

void StreamView::onFocusGained() {
    brls::Box::onFocusGained();
    brls::Application::getPlatform()->disableScreenDimming(true);
    brls::Application::getPlatform()->getInputManager()->setPointerLock(true);
}

void StreamView::onFocusLost() {
    CancelKeyboardShortcut();
    CancelNteAutoLogin();
    brls::Box::onFocusLost();
    brls::Application::getPlatform()->disableScreenDimming(false);
    brls::Application::getPlatform()->getInputManager()->setPointerLock(false);
}

void StreamView::ExitStream(bool end_cloud) {
    if (exit_requested_)
        return;
    exit_requested_ = true;
    CancelKeyboardShortcut();
    CancelNteAutoLogin();
    preserve_cloud_session_ = !end_cloud;
    if (recovery_job_) recovery_job_->cancelled.store(true);
    if (session_)
        session_->request_stop();
    StopCloudSessionAsync();
    if (!brls::Application::popActivity(brls::TransitionAnimation::FADE, [] {
            const auto activities = brls::Application::getActivitiesStack();
            if (!activities.empty() && activities.back() &&
                activities.back()->getContentView()) {
                brls::Application::giveFocus(activities.back()->getContentView());
            }
        })) {
        exit_requested_ = false;
    }
}

void StreamView::StopCloudSessionAsync() {
    if (preserve_cloud_session_ || cloud_stop_requested_ || cloud_session_id_.empty())
        return;

    cloud_stop_requested_ = true;
    opennow::GfnClient bg_client = client_;
    opennow::AuthSession bg_auth = auth_;
    std::string bg_session_id = cloud_session_id_;
    brls::async([bg_client, bg_auth, bg_session_id]() mutable {
        try {
            bg_client.StopSession(bg_auth, bg_session_id);
        } catch (...) {
        }
    });
}

void StreamView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    ReleaseEpicKey();
    if (recovering_) UpdateKeyboardShortcut(std::chrono::steady_clock::now());
    if (DrawRecovery(vg, x, y, width, height)) return;
    const auto ui_logic_begin=std::chrono::steady_clock::now();
    if (session_) {
        // Sample HID before signaling maintenance, so an occasionally busy
        // peer lock cannot postpone the controller state seen this frame.
        const auto input_now = std::chrono::steady_clock::now();
        performance_recorder_.ObserveTick(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(input_now.time_since_epoch()).count()));
        PollControllerStates(input_now);
        UpdateInlineKeyboard();

        const auto stream_now = std::chrono::steady_clock::now();
        UpdateSessionLimitNotice(stream_now);
        UpdateStreamEndState(stream_now);
        UpdateKeyboardShortcut(input_now);
        if (stream_end_reason_ != opennow::StreamEndReason::None &&
            stream_now >= stream_auto_exit_at_) {
            ExitStream();
            return;
        }
        
        brls::ControllerState state;
        brls::Application::getPlatform()->getInputManager()->updateUnifiedControllerState(&state);
        const bool auto_choice_owned=UpdateAutoLoginChoice(controller_connected_[0]?controller_states_[0]:state,input_now);
        if(!AutoLoginActive())nte_cancel_was_down_=state.buttons[brls::BUTTON_B];

        if (stream_end_reason_ == opennow::StreamEndReason::StreamEnded) {
            const bool return_down = state.buttons[brls::BUTTON_A] ||
                                     state.buttons[brls::BUTTON_B];
            // Require release after the notice appears: a held game button
            // must not dismiss the server's end message on the same frame.
            if (!return_down)
                stream_end_return_ready_ = true;
            else if (stream_end_return_ready_) {
                session_->record_ui_event("stream_end return_to_menu=button");
                ExitStream();
                return;
            }
        }

        bool nte_owned_input = auto_choice_owned;
        if (!auto_choice_owned && !stream_established_ && !stream_overlay_visible_ && !keyboard_visible_ &&
            stream_end_reason_ == opennow::StreamEndReason::None) {
            const bool a = state.buttons[brls::BUTTON_A];
            const bool b = state.buttons[brls::BUTTON_B];
            if (!a && !b) startup_controls_ready_ = true;
            else if (startup_controls_ready_) {
                startup_controls_ready_ = false;
                if (b) { ExitStream(false); return; }
                if (stream_now - initial_transport_started_at_ >= std::chrono::seconds(10)) {
                    StartRecovery(stream_now);
                    return;
                }
            }
        }
        if (AutoLoginActive() && stream_end_reason_ != opennow::StreamEndReason::None)
            CancelNteAutoLogin();
        if (!auto_choice_owned && AutoLoginActive() && !stream_overlay_visible_ &&
            stream_end_reason_ == opennow::StreamEndReason::None) {
            const bool was_active = AutoLoginActive();
            const bool cancel_down = state.buttons[brls::BUTTON_B];
            if (was_active && cancel_down && !nte_cancel_was_down_)
                CancelNteAutoLogin();
            else if (was_active)
                UpdateNteAutoLogin(input_now);
            nte_cancel_was_down_ = cancel_down;
            nte_owned_input = was_active;
        }

        const bool minus_down = state.buttons[brls::BUTTON_BACK];
        const bool plus_down_for_overlay = state.buttons[brls::BUTTON_START];
        const auto chord_now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            input_now.time_since_epoch()).count();
        const auto overlay_decision = opennow::input::EvaluateOverlayChord(
            minus_down, plus_down_for_overlay, overlay_chord_state_, chord_now_ms);
        overlay_chord_state_ = overlay_decision.next_state;
        if (!minus_down && !plus_down_for_overlay)
            overlay_chord_latched_ = false;
        if (overlay_decision.toggle_overlay && !overlay_chord_latched_ &&
            !keyboard_visible_ && !nte_owned_input &&
            stream_end_reason_ == opennow::StreamEndReason::None)
        {
            overlay_chord_latched_ = true;
            SetStreamOverlayVisible(!stream_overlay_visible_);
        }

        if (stream_overlay_visible_)
        {
            const bool b_down = state.buttons[brls::BUTTON_B];
            const bool a_down = state.buttons[brls::BUTTON_A];
            const bool up_down = state.buttons[brls::BUTTON_UP];
            const bool down_down = state.buttons[brls::BUTTON_DOWN];
            if (up_down && !stream_overlay_up_was_down_)
                stream_overlay_selection_ = opennow::input::MoveOverlaySelection(
                    stream_overlay_selection_, -1);
            if (down_down && !stream_overlay_down_was_down_)
                stream_overlay_selection_ = opennow::input::MoveOverlaySelection(
                    stream_overlay_selection_, 1);
            if (a_down && !stream_overlay_a_was_down_)
                ActivateStreamOverlayAction(input_now);
            if (b_down && !stream_overlay_b_was_down_)
            {
                suppress_b_until_release_ = true;
                SetStreamOverlayVisible(false);
            }
            stream_overlay_b_was_down_ = b_down;
            stream_overlay_a_was_down_ = a_down;
            stream_overlay_up_was_down_ = up_down;
            stream_overlay_down_was_down_ = down_down;
        }
        else
        {
            stream_overlay_b_was_down_ = false;
            stream_overlay_a_was_down_ = false;
            stream_overlay_up_was_down_ = false;
            stream_overlay_down_was_down_ = false;
        }

        const auto& command_state=controller_connected_[0]?controller_states_[0]:state;
        const bool exit_combo = minus_down && state.buttons[brls::BUTTON_LT] &&
            state.buttons[brls::BUTTON_RT] && !command_state.buttons[brls::BUTTON_RSB];
        if(exit_combo&&!exit_combo_was_down_&&!keyboard_visible_&&!stream_overlay_visible_&&
            !nte_owned_input&&!shortcut_router_.Armed()&&stream_end_reason_==opennow::StreamEndReason::None) {
            exit_combo_was_down_=true;ExitStream();return;
        }
        exit_combo_was_down_=exit_combo;
        const auto command=shortcut_router_.Update({
            command_state.buttons[brls::BUTTON_BACK],command_state.buttons[brls::BUTTON_START],
            command_state.buttons[brls::BUTTON_RSB],command_state.buttons[brls::BUTTON_LT],
            command_state.buttons[brls::BUTTON_LB],command_state.buttons[brls::BUTTON_RB],
            command_state.buttons[brls::BUTTON_RT],command_state.buttons[brls::BUTTON_Y],command_state.buttons[brls::BUTTON_B],
            AutoLoginAvailable()&&command_state.buttons[brls::BUTTON_X]},
            !stream_overlay_visible_&&!nte_owned_input&&!AutoLoginActive()&&
                stream_end_reason_==opennow::StreamEndReason::None,chord_now_ms);
        if(command.became_armed)brls::Application::notify(opennow::Tr(
            "Commands ready. Release MINUS + R3 if desired; choose within 5s. B cancels."));
        if(command.shortcut)SendKeyboardShortcut(*command.shortcut);
        if(command.start_autologin)StartAutoLogin(input_now);
        bool keyboard_owned_input = keyboard_visible_ || stream_overlay_visible_ ||
            overlay_decision.preempt_input || overlay_chord_latched_ ||
            command.capture || shortcut_sequence_.Active() ||
            nte_owned_input || AutoLoginActive() || stream_end_reason_ != opennow::StreamEndReason::None;
        if (command.open_keyboard && !shortcut_sequence_.Active() && !nte_owned_input &&
            !stream_overlay_visible_ &&
            stream_end_reason_ == opennow::StreamEndReason::None) {
            OpenInlineKeyboard();
            keyboard_owned_input = true;
        }

        if (keyboard_visible_) {
            const bool b_down = state.buttons[brls::BUTTON_B];
            const bool plus_down = state.buttons[brls::BUTTON_START];
            if (plus_down && !keyboard_plus_was_down_)
                HideInlineKeyboard(true);
            else if (b_down && !keyboard_b_was_down_)
                HideInlineKeyboard(false);
            keyboard_b_was_down_ = b_down;
            keyboard_plus_was_down_ = plus_down;
            keyboard_owned_input = true;
        } else {
            keyboard_b_was_down_ = false;
            keyboard_plus_was_down_ = false;
        }

        if (!state.buttons[brls::BUTTON_B])
            suppress_b_until_release_ = false;

        if (keyboard_release_guard_) {
            const bool controller_active =
                state.buttons[brls::BUTTON_A] || state.buttons[brls::BUTTON_B] ||
                state.buttons[brls::BUTTON_X] || state.buttons[brls::BUTTON_Y] ||
                state.buttons[brls::BUTTON_UP] || state.buttons[brls::BUTTON_DOWN] ||
                state.buttons[brls::BUTTON_LEFT] || state.buttons[brls::BUTTON_RIGHT] ||
                state.buttons[brls::BUTTON_LB] || state.buttons[brls::BUTTON_RB] ||
                state.buttons[brls::BUTTON_LT] || state.buttons[brls::BUTTON_RT] ||
                state.buttons[brls::BUTTON_START] || state.buttons[brls::BUTTON_BACK] ||
                std::fabs(state.axes[brls::LEFT_X]) > 0.15f ||
                std::fabs(state.axes[brls::LEFT_Y]) > 0.15f ||
                std::fabs(state.axes[brls::RIGHT_X]) > 0.15f ||
                std::fabs(state.axes[brls::RIGHT_Y]) > 0.15f;
            if (controller_active)
                keyboard_owned_input = true;
            else
                keyboard_release_guard_ = false;
        }

        if(keyboard_owned_input&&!keyboard_input_owned_last_) {
            SendNeutralControllerReports();
            ResetControllerDeliveryState();
            if(touch_was_down_){session_->send_mouse_left_button(false);touch_was_down_=false;}
        }
        keyboard_input_owned_last_=keyboard_owned_input;
        if (!keyboard_owned_input) {
        
        const auto now = std::chrono::steady_clock::now();

        // Preserve the requested Minus -> View mapping. Exit the stream with
        // a deliberate ZL + ZR + Minus combination instead of stealing B.

        SendControllerInputs(now);
        session_->poll();

        std::vector<brls::RawTouchState> touches;
        brls::Application::getPlatform()->getInputManager()->updateTouchStates(&touches);
        bool touch_down = false;
        brls::Point touch_position {};
        for (const auto& touch : touches) {
            if (touch.pressed) {
                touch_down = true;
                touch_position = touch.position;
                break;
            }
        }

        if (touch_down) {
            const int stream_width = std::max(1, session_->stream_width());
            const int stream_height = std::max(1, session_->stream_height());
            const auto target = opennow::input::MapTouchToStream(
                touch_position.x, touch_position.y, x, y, width, height,
                stream_width, stream_height);

            if (!touch_pointer_initialized_ ||
                touch_pointer_stream_width_ != stream_width ||
                touch_pointer_stream_height_ != stream_height) {
                touch_pointer_x_ = stream_width * 0.5f;
                touch_pointer_y_ = stream_height * 0.5f;
                touch_pointer_stream_width_ = stream_width;
                touch_pointer_stream_height_ = stream_height;
                touch_pointer_initialized_ = true;
            }
            if (!touch_was_down_) {
                session_->record_ui_event(
                    "touch begin raw=" + std::to_string(touch_position.x) + "/" +
                    std::to_string(touch_position.y) + " target=" +
                    std::to_string(target.x) + "/" + std::to_string(target.y) +
                    " pointer=" + std::to_string(touch_pointer_x_) + "/" +
                    std::to_string(touch_pointer_y_) +
                    " frame=" + std::to_string(stream_width) + "x" +
                    std::to_string(stream_height) + " view=" +
                    std::to_string(width) + "x" + std::to_string(height));
                // GFN mouse input is relative and the remote cursor may have
                // moved independently. Re-anchor every new touch so the click
                // lands exactly where the finger is, then track drags locally.
                ReanchorRemotePointer(target.x, target.y);
                session_->send_mouse_left_button(true);
            } else {
                const int16_t dx = ClampMouseDelta(target.x - touch_pointer_x_);
                const int16_t dy = ClampMouseDelta(target.y - touch_pointer_y_);
                if (dx != 0 || dy != 0) {
                    session_->send_mouse_move(dx, dy);
                    touch_pointer_x_ = std::clamp(touch_pointer_x_ + dx, 0.0f,
                                                  static_cast<float>(stream_width - 1));
                    touch_pointer_y_ = std::clamp(touch_pointer_y_ + dy, 0.0f,
                                                  static_cast<float>(stream_height - 1));
                }
            }
        } else if (touch_was_down_) {
            session_->send_mouse_left_button(false);
        }
        touch_was_down_ = touch_down;
        } else {
            // Keyboard/menu/auto-login ownership must not stop SCTP ACKs,
            // heartbeat, input reactivation or signaling maintenance.
            session_->poll();
        }
    }
    
    // Basic drawing logic - if we have a frame, we draw it using GLVideoRenderer
    const auto ui_frame_begin=std::chrono::steady_clock::now();
    ui_logic_max_us_=std::max(ui_logic_max_us_,static_cast<uint64_t>(std::chrono::duration_cast<
        std::chrono::microseconds>(ui_frame_begin-ui_logic_begin).count()));
    bool got_frame = false;
    const int64_t video_target = session_ ? session_->video_target_rtp_timestamp() : AV_NOPTS_VALUE;
    AVFrameHolder::instance().get([this, vg, width, height, &got_frame](AVFrame* frame, uint64_t generation, bool reused) {
        if (session_) {
            session_->draw(vg, width, height, frame, generation);
            got_frame = true;
        }
    }, video_target);
    ui_frame_max_us_=std::max(ui_frame_max_us_,static_cast<uint64_t>(std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now()-ui_frame_begin).count()));
    if (session_)
        UpdatePerformanceCounter();
    
    if (!got_frame) {
        DrawPreparingStream(vg, x, y, width, height);
    } else if (debug_diagnostics_ && show_debug_overlay_) {
        DrawDebugOverlay(vg, x, y, width);
    }

    const auto notice_now = std::chrono::steady_clock::now();
    if ((debug_diagnostics_ || stats_overlay_enabled_) &&
        stream_end_reason_ == opennow::StreamEndReason::None)
        DrawPerformanceOverlay(vg, x, y);
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        DrawSessionLimitNotice(vg, x, y, width, notice_now);
    DrawStreamEndNotice(vg, x, y, width, notice_now);
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        DrawNteAutoLoginStatus(vg, x, y, width, height, notice_now);
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        DrawStreamOverlay(vg, x, y, width, height, notice_now);
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        DrawNetworkWarning(vg, x, y, width, height, notice_now);
    if (stream_end_reason_ == opennow::StreamEndReason::None)
        DrawControllerNotice(vg, x, y, width, notice_now);
    if (auto_login_choice_state_.Active())
        DrawAutoLoginChoice(vg,x,y,width,height);

    opennow::shortcut::DrawHomeArtworkToast(vg);

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

brls::View* StreamView::create(
    const std::string& signaling_url,
    const std::string& jwt_token,
    const std::string& session_id,
    const std::string& media_ip,
    int media_port,
    const std::vector<opennow::IceServerInfo>& ice_servers,
    const opennow::GfnClient& client,
    const opennow::AuthSession& auth,
    const std::string& game_title,
    const std::string& requested_region, const std::string& actual_server, bool resumed,
    const std::string& initial_server, unsigned redirects, opennow::gfn::cloud_session::ClientProfile profile,
    const std::string& launch_store) {
    return new StreamView(signaling_url, jwt_token, session_id, media_ip, media_port,
                          ice_servers, client, auth, game_title, requested_region, actual_server, resumed, initial_server, redirects, profile,launch_store);
}
