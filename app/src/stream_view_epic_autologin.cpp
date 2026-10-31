#include "StreamView.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"
#include "stream_diagnostics.hpp"
#include <algorithm>

bool StreamView::EpicAutoLoginAvailable() const {
    return epic_login_available_;
}
void StreamView::ReleaseEpicKey() {
    if(!epic_key_release_||!session_)return;
    const auto health=session_->get_transport_health();
    if(!health.input_ready||health.peer_terminal!=opennow::PeerTerminalKind::None)return;
    const auto key=*epic_key_release_;
    if(session_->send_keyboard_key(key.keycode,key.scancode,0,false)) {
        epic_key_release_.reset();
        if(!epic_flow_.Active())opennow::SetSensitiveInputLoggingSuppressed(false);
    }
}
void StreamView::CancelEpicAutoLogin() {
    if(!epic_flow_.Active()&&!epic_probe_&&!epic_key_release_)return;
    const bool was_running=epic_flow_.Active();
    epic_flow_.Cancel();
    if(epic_probe_)epic_probe_->active=false;
    epic_probe_.reset();ReleaseEpicKey();
    if(!epic_key_release_)opennow::SetSensitiveInputLoggingSuppressed(false);
    AppendAutoLoginTrace(was_running?"EPIC cancelled content=redacted":"EPIC cleanup content=redacted");
    nte_status_=opennow::Tr("Auto-login cancelled");
    nte_status_until_=std::chrono::steady_clock::now()+std::chrono::seconds(5);
}
void StreamView::StartEpicAutoLogin(std::chrono::steady_clock::time_point now) {
    using namespace opennow;
    if(!EpicAutoLoginAvailable()||AutoLoginActive()||shortcut_sequence_.Active()||keyboard_visible_||recovering_||!session_)return;
    const auto health=session_->get_transport_health();
    if(!stream_established_||!health.input_ready||health.peer_terminal!=PeerTerminalKind::None)return;
    const auto aspect=float(session_->stream_width())/std::max(1,session_->stream_height());
    if(aspect<1.74f||aspect>1.81f){nte_status_=Tr("Auto-login requires a 16:9 stream");nte_status_until_=now+std::chrono::seconds(5);return;}
    auto credentials=autologin::Load(autologin::GetProfile(autologin::Game::Epic));
    if(!epic_flow_.Start(std::move(credentials),now)) {
        nte_status_=Tr("Set Epic auto-login credentials on the game page");nte_status_until_=now+std::chrono::seconds(5);return;
    }
    try {epic_probe_=std::make_shared<autologin::epic::Probe>();}
    catch(...){epic_flow_.Cancel();return;}
    epic_trace_=true;epic_probe_logged_=0;nte_next_action_=now;game_login_last_stage_.clear();
    shortcut_router_.Disarm();
    SetSensitiveInputLoggingSuppressed(true);SendNeutralControllerReports();ResetControllerDeliveryState();
    if(touch_was_down_){session_->send_mouse_left_button(false);touch_was_down_=false;}
    nte_status_=Tr("Epic: checking the email screen");nte_status_until_=now+std::chrono::seconds(95);
    AppendAutoLoginTrace("EPIC start mode=replace_email visual_guard=two_samples content=redacted");
    LogRuntimeEvent("form_input","epic_started");
}
void StreamView::UpdateEpicAutoLogin(std::chrono::steady_clock::time_point now) {
    using namespace opennow;
    ReleaseEpicKey();
    if(!epic_flow_.Active()||!epic_probe_)return;
    if(epic_probe_->serial!=epic_probe_logged_) {
        epic_probe_logged_=epic_probe_->serial;
        AppendAutoLoginTrace("EPIC_SCREEN type="+std::to_string(static_cast<unsigned>(epic_probe_->screen))+" sample="+std::to_string(epic_probe_logged_)+" continue_y="+std::to_string(epic_probe_->continue_y));
    }
    const auto health=session_?session_->get_transport_health():StreamTransportHealth{};
    const bool ready=session_&&health.input_ready&&health.peer_terminal==PeerTerminalKind::None&&
        !recovering_&&health.video_started&&health.video_idle<std::chrono::milliseconds(1500);
    const auto action=epic_flow_.Tick(now,ready,epic_probe_->screen,epic_probe_->serial,epic_probe_->continue_y);
    if(epic_flow_.NeedsProbe())autologin::epic::RequestProbe(epic_probe_,now);
    if(action.kind==autologin::ActionKind::None)return;
    if(game_login_last_stage_!=action.stage){game_login_last_stage_=action.stage;AppendAutoLoginTrace("STAGE "+game_login_last_stage_);}
    const auto send=[this](input::KeyboardStroke key) {
        epic_key_release_=key;
        const bool down=session_->send_keyboard_key(key.keycode,key.scancode,key.modifiers,true);
        ReleaseEpicKey();return down&&!epic_key_release_;
    };
    bool accepted=true;
    switch(action.kind) {
    case autologin::ActionKind::Click:
        SendNteClick(action.point.x,action.point.y);
        if(std::string(action.stage)=="epic_continue")nte_status_=Tr("Epic: waiting for the password screen");
        break;
    case autologin::ActionKind::ClearField: accepted=send({'A',0x1e,2})&&send({8,0x0e,0});break;
    case autologin::ActionKind::Character: {
        input::KeyboardStroke key{};accepted=input::MapAsciiKey(action.character,key)&&send(key);break;
    }
    case autologin::ActionKind::Submitted:
    case autologin::ActionKind::Aborted: {
        const bool submitted=action.kind==autologin::ActionKind::Submitted;
        CancelEpicAutoLogin();
        nte_status_=Tr(submitted?"Epic: sign-in submitted; complete any verification manually":"Epic: screen not recognized or connection interrupted; open the email form and retry");
        nte_status_until_=now+std::chrono::seconds(8);
        LogRuntimeEvent("form_input",submitted?"epic_submitted":"epic_stopped");break;
    }
    default:break;
    }
    if(!accepted) {
        CancelEpicAutoLogin();nte_status_=Tr("Epic: input interrupted; open the email form and retry");
        nte_status_until_=now+std::chrono::seconds(8);LogRuntimeEvent("form_input","epic_input_stopped");
    }
}
