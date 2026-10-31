#include "StreamView.hpp"
#include "keyboard_input_policy.hpp"
#include "nte_autologin_log.hpp"
#include "stream_diagnostics.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

int16_t StreamView::ClampMouseDelta(float value)
{
    return static_cast<int16_t>(std::clamp<long>(std::lround(value), -32768L, 32767L));
}

#ifdef __SWITCH__
StreamView* StreamView::active_keyboard_view_ = nullptr;
#endif

void StreamView::SendKeyboardCharacter(char character) {
    if (!session_)
        return;
    if (shortcut_sequence_.Active()) {
        CancelKeyboardShortcut();
        if (shortcut_sequence_.Active()) return;
    }

    opennow::input::KeyboardStroke stroke {};
    if (character == '\b')
        stroke = {0x08, 0x0e, 0};
    else if (character == '\n' || character == '\r')
        stroke = {0x0d, 0x1c, 0};
    else if (!opennow::input::MapAsciiKey(character, stroke)) {
        session_->record_ui_event("keyboard unsupported UTF-8 byte=" +
                                  std::to_string(static_cast<unsigned char>(character)));
        return;
    }

    session_->send_keyboard_key(stroke.keycode, stroke.scancode, stroke.modifiers, true);
    session_->send_keyboard_key(stroke.keycode, stroke.scancode, stroke.modifiers, false);
}

void StreamView::SendKeyboardShortcut(opennow::input::KeyboardShortcut shortcut) {
    if (!session_ || AutoLoginActive() || shortcut_sequence_.Active()) return;
    const auto health=session_->get_transport_health();
    if (!health.input_ready || health.peer_terminal!=opennow::PeerTerminalKind::None) {
        opennow::LogRuntimeEvent("keys","blocked",std::string("key=")+opennow::input::ShortcutName(shortcut)+" reason=input_not_ready");
        return;
    }
    const auto now=std::chrono::steady_clock::now();
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if(!shortcut_sequence_.Start(shortcut,ms))return;
    shortcut_key_=shortcut;
    SendNeutralControllerReports();
    ResetControllerDeliveryState();
    if(touch_was_down_){session_->send_mouse_left_button(false);touch_was_down_=false;}
    opennow::LogRuntimeEvent("keys","start",std::string("key=")+opennow::input::ShortcutName(shortcut));
    UpdateKeyboardShortcut(now);
}

void StreamView::UpdateKeyboardShortcut(std::chrono::steady_clock::time_point now) {
    if(!shortcut_sequence_.Active()||!session_)return;
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const auto health=session_->get_transport_health();
    const bool ready=health.input_ready&&health.peer_terminal==opennow::PeerTerminalKind::None;
    if(!ready)shortcut_router_.Disarm();
    if(recovering_||stream_end_reason_!=opennow::StreamEndReason::None)shortcut_sequence_.Cancel(ms);
    const auto event=shortcut_sequence_.Next(ms,ready);
    if(event) {
        const auto& key=event->stroke;
        shortcut_sequence_.Commit(session_->send_keyboard_key(key.keycode,key.scancode,key.modifiers,event->pressed),ms);
    }
    if(!shortcut_sequence_.Active())opennow::LogRuntimeEvent("keys",shortcut_sequence_.Cancelled()?"cancelled":"complete",
        std::string("key=")+opennow::input::ShortcutName(shortcut_key_));
}

void StreamView::CancelKeyboardShortcut() {
    shortcut_router_.Disarm();
    if(!shortcut_sequence_.Active())return;
    if(!shortcut_sequence_.Cancelled())opennow::LogRuntimeEvent("keys","cancelled",
        std::string("key=")+opennow::input::ShortcutName(shortcut_key_));
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    shortcut_sequence_.Cancel(ms);
    // At most two held keys. Best-effort releases now; rejected releases stay
    // pending for the next healthy transport, including after reconnect.
    for(unsigned i=0;i<2&&shortcut_sequence_.Active()&&session_;++i){
        const auto health=session_->get_transport_health();
        const auto event=shortcut_sequence_.Next(ms,health.input_ready&&health.peer_terminal==opennow::PeerTerminalKind::None);
        if(!event)break;
        const auto& key=event->stroke;
        const bool sent=session_->send_keyboard_key(key.keycode,key.scancode,key.modifiers,false);
        shortcut_sequence_.Commit(sent,ms);if(!sent)break;
    }
}

void StreamView::SendNteClick(float normalized_x, float normalized_y) {
    if (!session_) {
        AppendAutoLoginTrace("CLICK skipped reason=no_session");
        return;
    }

    const int stream_width = std::max(1, session_->stream_width());
    const int stream_height = std::max(1, session_->stream_height());
    if (!touch_pointer_initialized_ ||
        touch_pointer_stream_width_ != stream_width ||
        touch_pointer_stream_height_ != stream_height) {
        touch_pointer_x_ = stream_width * 0.5f;
        touch_pointer_y_ = stream_height * 0.5f;
        touch_pointer_stream_width_ = stream_width;
        touch_pointer_stream_height_ = stream_height;
        touch_pointer_initialized_ = true;
    }

    const float target_x = std::clamp(normalized_x, 0.0f, 1.0f) * (stream_width - 1);
    const float target_y = std::clamp(normalized_y, 0.0f, 1.0f) * (stream_height - 1);
    AppendAutoLoginTrace(
        "CLICK normalized=" + std::to_string(normalized_x) + "/" +
        std::to_string(normalized_y) + " target=" + std::to_string(target_x) + "/" +
        std::to_string(target_y) + " anchor=top_left stream=" +
        std::to_string(stream_width) + "x" +
        std::to_string(stream_height));
    ReanchorRemotePointer(target_x, target_y);
    session_->send_mouse_left_button(true);
    session_->send_mouse_left_button(false);
}

void StreamView::ReanchorRemotePointer(float target_x, float target_y) {
    if (!session_)
        return;

    const int stream_width = std::max(1, session_->stream_width());
    const int stream_height = std::max(1, session_->stream_height());
    target_x = std::clamp(target_x, 0.0f, static_cast<float>(stream_width - 1));
    target_y = std::clamp(target_y, 0.0f, static_cast<float>(stream_height - 1));

    // GFN exposes relative mouse movement. Two oversized moves guarantee that
    // the remote cursor reaches the top-left edge regardless of its real
    // position, eliminating drift between our model and the streamed cursor.
    session_->send_mouse_move(-4096, -4096);
    session_->send_mouse_move(-4096, -4096);
    session_->send_mouse_move(ClampMouseDelta(target_x), ClampMouseDelta(target_y));
    touch_pointer_x_ = target_x;
    touch_pointer_y_ = target_y;
    touch_pointer_stream_width_ = stream_width;
    touch_pointer_stream_height_ = stream_height;
    touch_pointer_initialized_ = true;
}

void StreamView::ClearNteFocusedField() {
    if (!session_)
        return;
    // Modifier bit 1 is Ctrl in the NVST keyboard protocol. Clearing the
    // active field makes retries idempotent instead of appending credentials.
    constexpr uint16_t kControl = 0x0002;
    session_->send_keyboard_key('A', 0x1e, kControl, true);
    session_->send_keyboard_key('A', 0x1e, kControl, false);
    session_->send_keyboard_key(0x08, 0x0e, 0, true);
    session_->send_keyboard_key(0x08, 0x0e, 0, false);
    AppendAutoLoginTrace("FIELD clear select_all_backspace content=redacted");
}

void StreamView::SubmitNteFocusedField(const char* stage) {
    SendKeyboardCharacter('\n');
    opennow::AppendNteAutoLoginLog(
        "FORM submit method=enter stage=" + std::string(stage ? stage : "unknown"));
}

void StreamView::StartNteAutoLogin(std::chrono::steady_clock::time_point now) {
    if (shortcut_sequence_.Active()) return;
    if (epic_flow_.Active()||epic_key_release_) return;
    epic_trace_=false;
    if (game_login_profile_) { StartGameAutoLogin(now); return; }
    if (!is_nte_session_ || nte_stage_ != NteAutoLoginStage::Idle)
        return;
    if (keyboard_visible_) {
        nte_status_ = "Close the on-screen keyboard before NTE Auto-login";
        nte_status_until_ = now + std::chrono::seconds(4);
        return;
    }

    nte_credentials_ = opennow::LoadNteCredentials();
    if (!nte_credentials_.valid()) {
        opennow::ResetNteAutoLoginLog("Neverness to Everness");
        opennow::AppendNteAutoLoginLog("START rejected reason=credentials_missing_or_invalid");
        nte_status_ = "NTE Auto-login is not configured on the game page";
        nte_status_until_ = now + std::chrono::seconds(5);
        brls::Application::notify(opennow::Tr("Configure NTE Auto-login on the Neverness to Everness game page"));
        return;
    }

    nte_stage_ = NteAutoLoginStage::ClickEmailProvider;
    opennow::SetSensitiveInputLoggingSuppressed(true);
    opennow::ResetNteAutoLoginLog("Neverness to Everness");
    opennow::AppendNteAutoLoginLog(
        "FLOW revision=0.9.4.5 submit=email_enter,password_enter field_clear=ctrl_a_backspace");
    opennow::AppendNteAutoLoginLog("START accepted credentials=present_and_redacted");
    if (session_)
        opennow::AppendNteAutoLoginLog("TRANSPORT snapshot\n" + session_->get_debug_info());
    nte_next_action_ = now;
    nte_status_ = "NTE Auto-login: opening email sign-in";
    nte_status_until_ = now + std::chrono::seconds(20);
    if (session_)
        session_->record_ui_event("nte_autologin started");
}

void StreamView::CancelNteAutoLogin() {
    auto_login_choice_state_.Close();
    if(epic_flow_.Active()||epic_key_release_)CancelEpicAutoLogin();
    if (game_login_flow_.Active()) {
        game_login_flow_.Cancel();
        suppress_b_until_release_ = true;
        opennow::SetSensitiveInputLoggingSuppressed(false);
        AppendAutoLoginTrace("CANCEL input_sequence_stopped");
        nte_status_ = opennow::Tr("Auto-login cancelled");
        nte_status_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }
    if (nte_stage_ == NteAutoLoginStage::Idle)
        return;
    std::fill(nte_text_buffer_.begin(), nte_text_buffer_.end(), '\0');
    nte_text_buffer_.clear();
    nte_text_index_ = 0;
    nte_stage_ = NteAutoLoginStage::Idle;
    opennow::SetSensitiveInputLoggingSuppressed(false);
    opennow::AppendNteAutoLoginLog("CANCEL source=user_button_b");
    nte_status_ = "NTE Auto-login cancelled";
    nte_status_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    if (session_)
        session_->record_ui_event("nte_autologin cancelled");
}

void StreamView::UpdateNteAutoLogin(std::chrono::steady_clock::time_point now) {
    if(auto_login_choice_state_.Active())return;
    if(epic_flow_.Active()||epic_key_release_){UpdateEpicAutoLogin(now);return;}
    if (game_login_profile_) { UpdateGameAutoLogin(now); return; }
    if (nte_stage_ == NteAutoLoginStage::Idle || now < nte_next_action_)
        return;

    switch (nte_stage_) {
        case NteAutoLoginStage::ClickEmailProvider:
            opennow::AppendNteAutoLoginLog("STAGE click_email_provider");
            SendNteClick(0.59f, 0.43f);
            nte_stage_ = NteAutoLoginStage::WaitEmailPage;
            nte_next_action_ = now + std::chrono::milliseconds(2500);
            nte_status_ = "NTE Auto-login: waiting for email form";
            break;
        case NteAutoLoginStage::WaitEmailPage:
            opennow::AppendNteAutoLoginLog("STAGE email_page_wait_complete delay_ms=2500");
            nte_stage_ = NteAutoLoginStage::FocusEmail;
            nte_next_action_ = now;
            break;
        case NteAutoLoginStage::FocusEmail:
            opennow::AppendNteAutoLoginLog("STAGE focus_email_field");
            SendNteClick(0.55f, 0.35f);
            ClearNteFocusedField();
            nte_text_buffer_ = nte_credentials_.email;
            nte_text_index_ = 0;
            nte_stage_ = NteAutoLoginStage::TypeEmail;
            nte_next_action_ = now + std::chrono::milliseconds(220);
            nte_status_ = "NTE Auto-login: entering email";
            opennow::AppendNteAutoLoginLog("TYPE email begin content=redacted");
            break;
        case NteAutoLoginStage::TypeEmail:
            if (nte_text_index_ < nte_text_buffer_.size()) {
                SendKeyboardCharacter(nte_text_buffer_[nte_text_index_++]);
                nte_next_action_ = now + std::chrono::milliseconds(35);
            } else {
                nte_text_buffer_.clear();
                opennow::AppendNteAutoLoginLog("TYPE email complete content=redacted");
                nte_stage_ = NteAutoLoginStage::SubmitEmail;
                nte_next_action_ = now + std::chrono::milliseconds(300);
            }
            break;
        case NteAutoLoginStage::SubmitEmail:
            opennow::AppendNteAutoLoginLog("STAGE submit_email method=enter");
            SubmitNteFocusedField("email");
            nte_stage_ = NteAutoLoginStage::WaitPasswordPage;
            nte_next_action_ = now + std::chrono::milliseconds(5500);
            nte_status_ = "NTE Auto-login: waiting for password form";
            opennow::AppendNteAutoLoginLog(
                "GUARD password_input_deferred_until_page_transition delay_ms=5500");
            break;
        case NteAutoLoginStage::WaitPasswordPage:
            opennow::AppendNteAutoLoginLog("STAGE password_page_wait_complete delay_ms=5500");
            nte_stage_ = NteAutoLoginStage::FocusPassword;
            nte_next_action_ = now;
            break;
        case NteAutoLoginStage::FocusPassword:
            opennow::AppendNteAutoLoginLog("STAGE focus_password_field");
            SendNteClick(0.55f, 0.45f);
            ClearNteFocusedField();
            nte_text_buffer_ = nte_credentials_.password;
            nte_text_index_ = 0;
            nte_stage_ = NteAutoLoginStage::TypePassword;
            nte_next_action_ = now + std::chrono::milliseconds(220);
            nte_status_ = "NTE Auto-login: entering password";
            opennow::AppendNteAutoLoginLog("TYPE password begin content=redacted");
            break;
        case NteAutoLoginStage::TypePassword:
            if (nte_text_index_ < nte_text_buffer_.size()) {
                SendKeyboardCharacter(nte_text_buffer_[nte_text_index_++]);
                nte_next_action_ = now + std::chrono::milliseconds(35);
            } else {
                std::fill(nte_text_buffer_.begin(), nte_text_buffer_.end(), '\0');
                nte_text_buffer_.clear();
                opennow::AppendNteAutoLoginLog("TYPE password complete content=redacted");
                nte_stage_ = NteAutoLoginStage::SubmitLogin;
                nte_next_action_ = now + std::chrono::milliseconds(300);
            }
            break;
        case NteAutoLoginStage::SubmitLogin:
            opennow::AppendNteAutoLoginLog("STAGE submit_login method=enter");
            SubmitNteFocusedField("password");
            nte_stage_ = NteAutoLoginStage::Idle;
            opennow::SetSensitiveInputLoggingSuppressed(false);
            nte_status_ = "NTE Auto-login: sign-in submitted";
            nte_status_until_ = now + std::chrono::seconds(5);
            if (session_)
                session_->record_ui_event("nte_autologin submitted");
            opennow::AppendNteAutoLoginLog("COMPLETE login_submitted");
            break;
        case NteAutoLoginStage::Idle:
            break;
    }
}

void StreamView::HandleKeyboardText(const char* text) {
    const std::string updated = text ? text : "";
    size_t prefix = 0;
    while (prefix < keyboard_text_.size() && prefix < updated.size() &&
           keyboard_text_[prefix] == updated[prefix]) {
        prefix++;
    }

    for (size_t i = prefix; i < keyboard_text_.size(); ++i)
        SendKeyboardCharacter('\b');
    for (size_t i = prefix; i < updated.size(); ++i)
        SendKeyboardCharacter(updated[i]);
    keyboard_text_ = updated;
}

void StreamView::OpenInlineKeyboard() {
#ifdef __SWITCH__
    if (keyboard_visible_)
        return;

    if (!keyboard_launched_) {
        Result rc = swkbdInlineCreate(&inline_keyboard_);
        if (R_FAILED(rc)) {
            if (session_)
                session_->record_ui_event("keyboard create failed rc=" + std::to_string(rc));
            return;
        }

        rc = swkbdInlineLaunchForLibraryApplet(
            &inline_keyboard_, static_cast<u8>(SwkbdInlineMode_AppletDisplay), 1);
        if (R_FAILED(rc)) {
            swkbdInlineClose(&inline_keyboard_);
            if (session_)
                session_->record_ui_event("keyboard launch failed rc=" + std::to_string(rc));
            return;
        }

        keyboard_launched_ = true;
        active_keyboard_view_ = this;
        swkbdInlineSetChangedStringCallback(&inline_keyboard_, KeyboardChangedCallback);
        swkbdInlineSetDecidedEnterCallback(&inline_keyboard_, KeyboardEnterCallback);
        swkbdInlineSetDecidedCancelCallback(&inline_keyboard_, KeyboardCancelCallback);
        swkbdInlineSetAlphaEnabledInInputMode(&inline_keyboard_, true);
        swkbdInlineSetKeytopBgAlpha(&inline_keyboard_, 0.72f);
        swkbdInlineSetFooterBgAlpha(&inline_keyboard_, 0.62f);
        swkbdInlineSetKeytopAsFloating(&inline_keyboard_, true);
        swkbdInlineSetFooterScalable(&inline_keyboard_, true);
        swkbdInlineSetTouchFlag(&inline_keyboard_, true);
        swkbdInlineSetDirectionalButtonAssignFlag(&inline_keyboard_, true);
    }

    keyboard_text_.clear();
    swkbdInlineSetInputText(&inline_keyboard_, "");
    swkbdInlineSetCursorPos(&inline_keyboard_, 0);
    SwkbdAppearArg appear {};
    swkbdInlineMakeAppearArg(&appear, SwkbdType_Latin);
    swkbdInlineAppearArgSetStringLenMax(&appear, 500);
    swkbdInlineAppearArgSetOkButtonText(&appear, opennow::Tr("Enter").c_str());
    swkbdInlineAppear(&inline_keyboard_, &appear);
    keyboard_visible_ = true;
    keyboard_b_was_down_ = false;
    keyboard_plus_was_down_ = false;
    ResetControllerDeliveryState();
    if (touch_was_down_) {
        session_->send_mouse_left_button(false);
        touch_was_down_ = false;
    }
    SendNeutralControllerReports();
    session_->record_ui_event("keyboard opened by guarded command chord");
#endif
}

void StreamView::HideInlineKeyboard(bool send_enter) {
    CancelKeyboardShortcut();
#ifdef __SWITCH__
    if (!keyboard_visible_)
        return;
    if (send_enter)
        SendKeyboardCharacter('\n');
    swkbdInlineDisappear(&inline_keyboard_);
    keyboard_visible_ = false;
    keyboard_text_.clear();
    ResetControllerDeliveryState();
    suppress_b_until_release_ = true;
    keyboard_release_guard_ = true;
    if (session_)
        session_->record_ui_event(send_enter ? "keyboard submitted" : "keyboard hidden by B");
#else
    (void)send_enter;
#endif
}

void StreamView::UpdateInlineKeyboard() {
#ifdef __SWITCH__
    if (!keyboard_launched_)
        return;
    SwkbdState state = SwkbdState_Inactive;
    const Result rc = swkbdInlineUpdate(&inline_keyboard_, &state);
    if (R_FAILED(rc) && session_)
        session_->record_ui_event("keyboard update failed rc=" + std::to_string(rc));
#endif
}

#ifdef __SWITCH__
void StreamView::KeyboardChangedCallback(const char* text, SwkbdChangedStringArg*) {
    if (active_keyboard_view_)
        active_keyboard_view_->HandleKeyboardText(text);
}

void StreamView::KeyboardEnterCallback(const char* text, SwkbdDecidedEnterArg*) {
    if (!active_keyboard_view_)
        return;
    active_keyboard_view_->HandleKeyboardText(text);
    active_keyboard_view_->SendKeyboardCharacter('\n');
    active_keyboard_view_->keyboard_visible_ = false;
    active_keyboard_view_->keyboard_text_.clear();
    active_keyboard_view_->ResetControllerDeliveryState();
    active_keyboard_view_->keyboard_release_guard_ = true;
    if (active_keyboard_view_->session_)
        active_keyboard_view_->session_->record_ui_event("keyboard submitted");
}

void StreamView::KeyboardCancelCallback() {
    if (!active_keyboard_view_)
        return;
    active_keyboard_view_->CancelKeyboardShortcut();
    active_keyboard_view_->keyboard_visible_ = false;
    active_keyboard_view_->keyboard_text_.clear();
    active_keyboard_view_->ResetControllerDeliveryState();
    active_keyboard_view_->suppress_b_until_release_ = true;
    active_keyboard_view_->keyboard_release_guard_ = true;
    if (active_keyboard_view_->session_)
        active_keyboard_view_->session_->record_ui_event("keyboard cancelled");
}
#endif
