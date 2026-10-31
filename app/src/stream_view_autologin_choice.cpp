#include "StreamView.hpp"
#include "localization.hpp"
#include <algorithm>
bool StreamView::AutoLoginAvailable() const {return is_nte_session_||game_login_profile_||EpicAutoLoginAvailable();}
void StreamView::StartAutoLogin(std::chrono::steady_clock::time_point now) {
    if(!AutoLoginAvailable()||AutoLoginActive()||keyboard_visible_||shortcut_sequence_.Active()||!session_||recovering_)return;
    const auto health=session_->get_transport_health();
    if(!stream_established_||!health.input_ready||!health.video_started||
        health.video_idle>=std::chrono::milliseconds(1500)||health.peer_terminal!=opennow::PeerTerminalKind::None)return;
    shortcut_router_.Disarm();
    if(EpicAutoLoginAvailable()&&(is_nte_session_||game_login_profile_)) {
        auto_login_choice_state_.Open();
        SendNeutralControllerReports();ResetControllerDeliveryState();
        if(touch_was_down_){session_->send_mouse_left_button(false);touch_was_down_=false;}
    } else if(EpicAutoLoginAvailable())StartEpicAutoLogin(now);
    else StartNteAutoLogin(now);
}
bool StreamView::UpdateAutoLoginChoice(const brls::ControllerState& state,std::chrono::steady_clock::time_point now) {
    if(!auto_login_choice_state_.Active())return false;
    if(stream_end_reason_!=opennow::StreamEndReason::None){auto_login_choice_state_.Close();return true;}
    const bool a=state.buttons[brls::BUTTON_A],b=state.buttons[brls::BUTTON_B];
    const bool up=state.buttons[brls::BUTTON_UP],down=state.buttons[brls::BUTTON_DOWN];
    const auto decision=auto_login_choice_state_.Update(a,b,state.buttons[brls::BUTTON_X],up,down);
    if(decision.accepted||decision.cancelled) {
        keyboard_release_guard_=true;suppress_b_until_release_=true;
        if(decision.accepted){if(decision.target==opennow::autologin::Target::Epic)StartEpicAutoLogin(now);else StartNteAutoLogin(now);}
    }
    return true;
}
void StreamView::DrawAutoLoginChoice(NVGcontext* vg,float x,float y,float width,float height) {
    const float w=std::min(580.0f,width-40),h=248,cx=x+(width-w)/2,cy=y+(height-h)/2;
    nvgBeginPath(vg);nvgRect(vg,x,y,width,height);nvgFillColor(vg,nvgRGBA(0,0,0,180));nvgFill(vg);
    nvgBeginPath(vg);nvgRoundedRect(vg,cx,cy,w,h,16);nvgFillColor(vg,nvgRGB(16,22,26));nvgFill(vg);
    nvgFontFaceId(vg,brls::Application::getFont(brls::FONT_REGULAR));nvgFontSize(vg,23);
    nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);nvgFillColor(vg,nvgRGB(242,246,244));
    const auto title=opennow::Tr("Choose an auto-login profile");nvgText(vg,cx+24,cy+35,title.c_str(),nullptr);
    for(unsigned i=0;i<2;++i) {
        const float ry=cy+65+56*i;const bool selected=i==static_cast<unsigned>(auto_login_choice_state_.Selected());
        nvgBeginPath(vg);nvgRoundedRect(vg,cx+24,ry,w-48,46,8);nvgFillColor(vg,selected?nvgRGB(38,96,70):nvgRGB(30,37,42));nvgFill(vg);
        nvgFontSize(vg,21);nvgFillColor(vg,nvgRGB(245,248,246));
        auto label=opennow::Tr(i==0?"Epic Games account":"Game account");
        if(i==1)label+=" ("+std::string(is_nte_session_?"NTE":game_login_profile_?game_login_profile_->folder:"Game")+")";
        nvgText(vg,cx+40,ry+23,label.c_str(),nullptr);
    }
    nvgFontSize(vg,17);nvgFillColor(vg,nvgRGB(170,190,180));
    const auto hint=opennow::Tr("A: select / B: cancel");nvgText(vg,cx+24,cy+h-28,hint.c_str(),nullptr);
}
