#pragma once
#include <borealis.hpp>
#include "controller_assignment_policy.hpp"
#include "gfn_client.hpp"
#include "controller_delivery_policy.hpp"
#include "keyboard_input_policy.hpp"
#include "keyboard_shortcuts.hpp"
#include "network_utils.hpp"
#include "background_sampler.hpp"
#include "nte_credentials.hpp"
#include "game_autologin.hpp"
#include "epic_screen_probe.hpp"
#include "autologin_choice.hpp"
#include "stream_end_policy.hpp"
#include "stream_status_policy.hpp"
#include "performance_recorder.hpp"
#include "session_recovery_policy.hpp"
#include "stream_overlay_policy.hpp"
#include "webrtc_session.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

class StreamView : public brls::Box {
public:
    StreamView(
        const std::string& signaling_url,
        const std::string& jwt_token,
        const std::string& session_id,
        const std::string& media_ip,
        int media_port,
        const std::vector<opennow::IceServerInfo>& ice_servers,
        const opennow::GfnClient& client,
        const opennow::AuthSession& auth,
        const std::string& game_title,
    const std::string& requested_region, const std::string& actual_server, bool resumed = false,
    const std::string& initial_server = {}, unsigned redirects = 0,
    opennow::gfn::cloud_session::ClientProfile profile = opennow::gfn::cloud_session::ClientProfile::NativeWebRtc,
    const std::string& launch_store = {});
    ~StreamView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create(
        const std::string& signaling_url,
        const std::string& jwt_token,
        const std::string& session_id,
        const std::string& media_ip,
        int media_port,
        const std::vector<opennow::IceServerInfo>& ice_servers,
        const opennow::GfnClient& client,
        const opennow::AuthSession& auth,
        const std::string& game_title,
    const std::string& requested_region, const std::string& actual_server, bool resumed = false,
    const std::string& initial_server = {}, unsigned redirects = 0,
    opennow::gfn::cloud_session::ClientProfile profile = opennow::gfn::cloud_session::ClientProfile::NativeWebRtc,
    const std::string& launch_store = {});

private:
    void ExitStream(bool end_cloud = true);
    void StartRecovery(std::chrono::steady_clock::time_point now);
    bool DrawRecovery(NVGcontext* vg, float x, float y, float width, float height);
    void StopCloudSessionAsync();
    void DrawDebugOverlay(NVGcontext* vg, float x, float y, float width);
    void DrawPreparingStream(NVGcontext* vg, float x, float y, float width, float height);
    void UpdatePerformanceCounter();
    void DrawPerformanceOverlay(NVGcontext* vg, float x, float y);
    void SetStreamOverlayVisible(bool visible);
    void ActivateStreamOverlayAction(std::chrono::steady_clock::time_point now);
    void DrawStreamOverlay(NVGcontext* vg, float x, float y, float width, float height,
                           std::chrono::steady_clock::time_point now);
    void RefreshNetworkInfo(std::chrono::steady_clock::time_point now);
    void DrawNetworkWarning(NVGcontext* vg, float x, float y, float width, float height,
                            std::chrono::steady_clock::time_point now);
    void UpdateSessionLimitNotice(std::chrono::steady_clock::time_point now);
    void DrawSessionLimitNotice(NVGcontext* vg, float x, float y, float width,
                                std::chrono::steady_clock::time_point now);
    void UpdateStreamEndState(std::chrono::steady_clock::time_point now);
    void BeginStreamEnd(opennow::StreamEndReason reason,
                        std::chrono::steady_clock::time_point now);
    void DrawStreamEndNotice(NVGcontext* vg, float x, float y, float width,
                             std::chrono::steady_clock::time_point now);
    void PollControllerStates(std::chrono::steady_clock::time_point now);
    void SendControllerInputs(std::chrono::steady_clock::time_point now);
    void ResetControllerDeliveryState();
    void SendNeutralControllerReports();
    void QueueControllerConnectedNotice(
        std::size_t controller, std::chrono::steady_clock::time_point now);
    void QueueControllerDisconnectedNotice(
        std::size_t controller, std::chrono::steady_clock::time_point now);
    void UpdateControllerNotice(std::chrono::steady_clock::time_point now);
    void DrawControllerNotice(NVGcontext* vg, float x, float y, float width,
                              std::chrono::steady_clock::time_point now);
#ifdef __SWITCH__
    bool ReadSwitchControllerSource(std::size_t source, brls::ControllerState& state);
    void SampleSwitchControllers();
#endif
    void OpenInlineKeyboard();
    void HideInlineKeyboard(bool send_enter);
    void UpdateInlineKeyboard();
    void HandleKeyboardText(const char* text);
    void SendKeyboardCharacter(char character);
    void SendKeyboardShortcut(opennow::input::KeyboardShortcut shortcut);
    void UpdateKeyboardShortcut(std::chrono::steady_clock::time_point now);
    void CancelKeyboardShortcut();
    void StartNteAutoLogin(std::chrono::steady_clock::time_point now);
    void CancelNteAutoLogin();
    void UpdateNteAutoLogin(std::chrono::steady_clock::time_point now);
    void SendNteClick(float normalized_x, float normalized_y);
    void ReanchorRemotePointer(float target_x, float target_y);
    static int16_t ClampMouseDelta(float value);
    void ClearNteFocusedField();
    void SubmitNteFocusedField(const char* stage);
    void StartGameAutoLogin(std::chrono::steady_clock::time_point now);
    void UpdateGameAutoLogin(std::chrono::steady_clock::time_point now);
    bool AutoLoginActive() const;
    void AppendAutoLoginTrace(const std::string& line);
    void StartEpicAutoLogin(std::chrono::steady_clock::time_point now);
    void UpdateEpicAutoLogin(std::chrono::steady_clock::time_point now);
    void CancelEpicAutoLogin();
    void ReleaseEpicKey();
    bool EpicAutoLoginAvailable() const;
    bool AutoLoginAvailable() const;
    void StartAutoLogin(std::chrono::steady_clock::time_point now);
    bool UpdateAutoLoginChoice(const brls::ControllerState&,std::chrono::steady_clock::time_point now);
    void DrawAutoLoginChoice(NVGcontext*,float,float,float,float);
    void DrawNteAutoLoginStatus(NVGcontext* vg, float x, float y, float width, float height,
                                std::chrono::steady_clock::time_point now);
#ifdef __SWITCH__
    static void KeyboardChangedCallback(const char* text, SwkbdChangedStringArg* arg);
    static void KeyboardEnterCallback(const char* text, SwkbdDecidedEnterArg* arg);
    static void KeyboardCancelCallback();
    static StreamView* active_keyboard_view_;
#endif

    enum class SessionLimitNotice {
        None,
        FiveMinutes,
        OneMinute,
        Ended,
    };

    enum class NteAutoLoginStage {
        Idle,
        ClickEmailProvider,
        WaitEmailPage,
        FocusEmail,
        TypeEmail,
        SubmitEmail,
        WaitPasswordPage,
        FocusPassword,
        TypePassword,
        SubmitLogin,
    };

    struct ControllerDeliveryState {
        bool initialized = false;
        bool pending_disconnect = false;
        bool plus_was_down = false;
        bool plus_long_press = false;
        uint16_t last_buttons = 0;
        uint8_t last_left_trigger = 0;
        uint8_t last_right_trigger = 0;
        int16_t last_lx = 0;
        int16_t last_ly = 0;
        int16_t last_rx = 0;
        int16_t last_ry = 0;
        std::chrono::steady_clock::time_point plus_pressed_at {};
        std::chrono::steady_clock::time_point last_report {};
        opennow::input::StartDeliveryPulse start_pulse;
    };

    std::unique_ptr<WebRtcSession> session_;
    opennow::GfnClient client_;
    opennow::AuthSession auth_;
    opennow::StreamSettings session_settings_;
    std::string cloud_session_id_;
    std::string game_title_;
    bool cloud_stop_requested_ = false;
    struct RecoveryJob {
        std::atomic<bool> cancelled {false};
        std::atomic<bool> done {false};
        opennow::SessionInfo info;
        opennow::AuthSession auth;
        std::string error;
        bool terminal = false;
    };
    std::shared_ptr<RecoveryJob> recovery_job_;
    opennow::recovery::Budget recovery_budget_;
    bool preserve_cloud_session_ = false;
    bool recovering_ = false;
    bool recovery_failed_ = false;
    bool recovery_terminal_ = false;
    bool stream_established_ = false;
    std::chrono::steady_clock::time_point initial_transport_started_at_ {};
    bool recovery_a_down_ = false, recovery_b_down_ = false, recovery_x_down_ = false;
    bool recovery_input_ready_ = false;
    std::string recovery_error_;
    std::chrono::steady_clock::time_point recovery_connect_deadline_ {};
    std::chrono::steady_clock::time_point recovery_retry_at_ {};
    bool debug_diagnostics_ = false;
    bool stats_overlay_enabled_ = false;
    bool stream_overlay_visible_ = false;
    bool stream_overlay_b_was_down_ = false;
    bool stream_overlay_a_was_down_ = false;
    bool stream_overlay_up_was_down_ = false;
    bool stream_overlay_down_was_down_ = false;
    std::size_t stream_overlay_selection_ = 0;
    std::string stream_overlay_status_;
    std::chrono::steady_clock::time_point stream_overlay_status_until_ {};
    bool overlay_chord_latched_ = false;
    opennow::input::OverlayChordState overlay_chord_state_;
    std::string stream_codec_ = "H264";
    std::string stream_region_ = "Auto";
    std::string actual_server_, media_endpoint_, initial_server_;
    unsigned server_redirects_ = 0;
    std::string controller_layout_ = "Xbox";
    bool show_debug_overlay_ = false;
    bool is_nte_session_ = false;
    std::string launch_store_;
    bool epic_trace_ = false;
    opennow::autologin::Choice auto_login_choice_state_;
    bool epic_login_available_ = false;
    std::uint64_t epic_probe_logged_ = 0;
    opennow::autologin::epic::Flow epic_flow_;
    std::shared_ptr<opennow::autologin::epic::Probe> epic_probe_;
    std::optional<opennow::input::KeyboardStroke> epic_key_release_;
    const opennow::autologin::Profile* game_login_profile_ = nullptr;
    opennow::autologin::Flow game_login_flow_;
    std::string game_login_last_stage_;
    bool nte_cancel_was_down_ = false;
    NteAutoLoginStage nte_stage_ = NteAutoLoginStage::Idle;
    opennow::NteCredentials nte_credentials_;
    std::string nte_text_buffer_;
    size_t nte_text_index_ = 0;
    std::string nte_status_;
    std::chrono::steady_clock::time_point nte_next_action_ {};
    std::chrono::steady_clock::time_point nte_status_until_ {};
    bool exit_requested_ = false;
    bool exit_combo_was_down_ = false;
    bool touch_was_down_ = false;
    bool touch_pointer_initialized_ = false;
    float touch_pointer_x_ = 0.0f;
    float touch_pointer_y_ = 0.0f;
    int touch_pointer_stream_width_ = 0;
    int touch_pointer_stream_height_ = 0;
    opennow::input::ShortcutRouter shortcut_router_;
    opennow::input::ShortcutSequence shortcut_sequence_;
    opennow::input::KeyboardShortcut shortcut_key_ = opennow::input::KeyboardShortcut::Escape;
    bool keyboard_input_owned_last_ = false;
    bool keyboard_b_was_down_ = false;
    bool keyboard_plus_was_down_ = false;
    bool suppress_b_until_release_ = false;
    bool keyboard_release_guard_ = false;
    bool keyboard_visible_ = false;
    bool keyboard_launched_ = false;
    std::string keyboard_text_;
#ifdef __SWITCH__
    SwkbdInline inline_keyboard_ {};
#endif
    opennow::input::ControllerAssignments controller_assignments_;
    std::array<brls::ControllerState, opennow::input::kRemoteControllerCount>
        controller_states_ {};
    std::array<bool, opennow::input::kRemoteControllerCount> controller_connected_ {};
    std::array<ControllerDeliveryState, opennow::input::kRemoteControllerCount>
        controller_delivery_ {};
    bool controller_connections_initialized_ = false;
    std::string controller_notice_text_;
    std::deque<std::string> controller_notice_queue_;
    std::chrono::steady_clock::time_point controller_notice_visible_until_ {};
#ifdef __SWITCH__
    std::array<PadState, opennow::input::kSwitchControllerSourceCount>
        switch_controller_sources_ {};
    std::array<brls::ControllerState, opennow::input::kSwitchControllerSourceCount>
        sampled_controller_states_ {};
    std::array<bool, opennow::input::kSwitchControllerSourceCount>
        sampled_controller_connected_ {};
    std::array<brls::ControllerState, opennow::input::kSwitchControllerSourceCount>
        latched_controller_edges_ {};
    std::mutex controller_sample_mutex_;
    std::atomic<bool> controller_sampler_running_ {false};
    std::thread controller_sampler_thread_;
    bool swap_half_joycon_stick_to_dpad_ = false;
#endif
    std::chrono::steady_clock::time_point fps_window_started_ {};
    VideoPerformanceCounters previous_video_counters_ {};
    opennow::status::Assessment performance_assessment_;
    opennow::performance::Recorder performance_recorder_;
    LivePauseStats previous_pause_stats_ {};
    ClientPerformanceCounters previous_client_stats_ {};
    unsigned performance_transport_epoch_ = 0;
    bool startup_controls_ready_ = false;
    float incoming_fps_ = 0.0f;
    float decoded_fps_ = 0.0f;
    float presented_fps_ = 0.0f;
    float stream_bitrate_mbps_ = 0.0f;
    int network_rtt_ms_ = -1;
    StreamNetworkCounters network_counters_ {};
    uint32_t recent_video_drops_ = 0;
    bool free_tier_session_ = false;
    bool five_minute_warning_shown_ = false;
    bool one_minute_warning_shown_ = false;
    bool session_limit_ended_ = false;
    SessionLimitNotice session_limit_notice_ = SessionLimitNotice::None;
    std::chrono::steady_clock::time_point stream_started_at_ {};
    std::chrono::steady_clock::time_point session_notice_visible_until_ {};
    opennow::StreamEndReason stream_end_reason_ = opennow::StreamEndReason::None;
    bool stream_end_return_ready_ = false;
    std::chrono::steady_clock::time_point stream_end_started_at_ {};
    std::chrono::steady_clock::time_point stream_auto_exit_at_ {};
    std::chrono::steady_clock::time_point last_network_check_at_ {};
    std::chrono::steady_clock::time_point last_network_info_check_at_ {};
    std::chrono::steady_clock::time_point network_warning_visible_until_ {};
    std::unique_ptr<opennow::BackgroundSampler<opennow::NetworkConnectionInfo>> network_monitor_;
    uint64_t ui_logic_max_us_=0, ui_frame_max_us_=0;
    opennow::NetworkConnectionInfo network_info_ {};
    bool internet_connected_ = true;
    std::chrono::steady_clock::time_point network_unavailable_since_ {};
};
