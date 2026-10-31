#include "StreamView.hpp"
#include "nte_autologin_log.hpp"
#include "stream_diagnostics.hpp"
#include "runtime_journal.hpp"
#include "localization.hpp"
#include <algorithm>
#include <fstream>
#include <sys/stat.h>

bool StreamView::AutoLoginActive() const {
    return auto_login_choice_state_.Active() || nte_stage_ != NteAutoLoginStage::Idle || game_login_flow_.Active() || epic_flow_.Active() || epic_key_release_.has_value();
}
void StreamView::AppendAutoLoginTrace(const std::string& line) {
    const auto* trace_profile=epic_trace_?&opennow::autologin::GetProfile(opennow::autologin::Game::Epic):game_login_profile_;
    if (!trace_profile) { opennow::AppendNteAutoLoginLog(line); return; }
    if (!opennow::StreamDiagnosticsEnabled()) return;
    // Only fixed stage names, coordinates and redaction notices reach this writer.
    const auto directory = opennow::autologin::Directory(*trace_profile);
#ifdef __SWITCH__
    mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/SwitchNOW",0777);mkdir(directory.c_str(),0777);
#endif
    const auto path = directory + "/autologin.log";
    std::ifstream prior(path, std::ios::binary | std::ios::ate);
    const bool rotate = prior && prior.tellg() > 256 * 1024;
    prior.close();
    std::ofstream log(path, std::ios::binary | (rotate ? std::ios::trunc : std::ios::app));
    if(log)log << "[+" << std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-nte_next_action_).count() << "ms] " << line << '\n';
}
void StreamView::StartGameAutoLogin(std::chrono::steady_clock::time_point now) {
    using namespace opennow;
    if (!game_login_profile_ || AutoLoginActive()) return;
    epic_trace_=false;
    const auto health = session_ ? session_->get_transport_health() : StreamTransportHealth{};
    if (keyboard_visible_ || !session_ || !health.input_ready || !health.peer_completed ||
        health.peer_terminal != opennow::PeerTerminalKind::None ||
        !stream_established_ || recovering_) {
        nte_status_=Tr("Auto-login: wait for the stream and close the keyboard");
        nte_status_until_=now+std::chrono::seconds(5);return;
    }
    const float aspect = static_cast<float>(session_->stream_width()) / std::max(1,session_->stream_height());
    if (aspect < 1.74f || aspect > 1.81f) {
        nte_status_=Tr("Auto-login requires a 16:9 stream");
        nte_status_until_=now+std::chrono::seconds(5);return;
    }
    auto credentials=autologin::Load(*game_login_profile_);
    nte_next_action_=now;
    const bool started=game_login_flow_.Start(*game_login_profile_,std::move(credentials),now);
    if(!started) {
        nte_status_=Tr("Set auto-login credentials on the game page");
        nte_status_until_=now+std::chrono::seconds(5);
        AppendAutoLoginTrace("START rejected credentials_missing_or_unsupported");return;
    }
    SendNeutralControllerReports();
    if(touch_was_down_)session_->send_mouse_left_button(false);
    touch_was_down_=false;
    SetSensitiveInputLoggingSuppressed(true);
    game_login_last_stage_.clear();
    nte_status_=Tr("Auto-login: entering credentials");
    nte_status_until_=now+std::chrono::seconds(65);
    AppendAutoLoginTrace("START R67 supplied_screen_profile credentials=redacted");
    LogRuntimeEvent("form_input","started",std::string("game=")+game_login_profile_->folder);
}
void StreamView::UpdateGameAutoLogin(std::chrono::steady_clock::time_point now) {
    using namespace opennow;
    const auto health = session_ ? session_->get_transport_health() : StreamTransportHealth{};
    const auto action=game_login_flow_.Tick(now,session_ && health.input_ready && health.peer_completed &&
        health.peer_terminal == opennow::PeerTerminalKind::None && !recovering_);
    if(action.kind==autologin::ActionKind::None)return;
    if(game_login_last_stage_!=action.stage) {
        game_login_last_stage_=action.stage;AppendAutoLoginTrace("STAGE "+game_login_last_stage_);
    }
    switch(action.kind) {
    case autologin::ActionKind::Click: SendNteClick(action.point.x,action.point.y);break;
    case autologin::ActionKind::ClearField: ClearNteFocusedField();break;
    case autologin::ActionKind::Character: SendKeyboardCharacter(action.character);break;
    case autologin::ActionKind::Submitted:
    case autologin::ActionKind::Aborted:
        SetSensitiveInputLoggingSuppressed(false);
        nte_status_=Tr(action.kind==autologin::ActionKind::Submitted ? "Auto-login: sign-in submitted" : "Auto-login stopped; retry on the sign-in screen");
        nte_status_until_=now+std::chrono::seconds(5);
        LogRuntimeEvent("form_input",action.stage,std::string("game=")+game_login_profile_->folder);
        ResetControllerDeliveryState();break;
    default: break;
    }
}
