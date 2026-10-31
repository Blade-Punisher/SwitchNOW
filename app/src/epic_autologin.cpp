#include "epic_autologin.hpp"
#include <algorithm>
#include <cctype>
namespace opennow::autologin::epic {
namespace {
std::string Key(const std::string& s){std::string out;for(unsigned char c:s)if(c<128&&std::isalnum(c))out+=static_cast<char>(std::tolower(c));return out;}
bool Cyan(Color p){return p.r<85&&p.g>165&&p.b>205&&int(p.b)>int(p.r)+70&&int(p.g)>int(p.r)+50;}
bool Dark(Color p){return std::max({p.r,p.g,p.b})<52;}
bool Grey(Color p){auto hi=std::max({p.r,p.g,p.b}),lo=std::min({p.r,p.g,p.b});return hi-lo<20&&lo>=45&&hi<=110;}
}
bool Eligible(const std::string& title,const std::string& selected_store) {
    const auto name=Key(title),store=Key(selected_store);
    const bool fortnite=name.find("fortnite")!=std::string::npos||
        title.find("堡垒之夜")!=std::string::npos||title.find("堡壘之夜")!=std::string::npos||
        title.find("Фортнайт")!=std::string::npos||title.find("фортнайт")!=std::string::npos||
        title.find("ФОРТНАЙТ")!=std::string::npos||title.find("フォートナイト")!=std::string::npos||title.find("포트나이트")!=std::string::npos;
    return !title.empty()&&!fortnite&&
        (store=="epic"||store=="epicgames"||store=="epicgamesstore"||store=="epicgameslauncher");
}
Detection Detect(const Samples& s) {
    unsigned field=0,upper_dark=0,lower_blue=0,lower_grey=0;
    for(unsigned i=0;i<2;++i)field+=Dark(s[i]);
    for(unsigned i=2;i<6;++i)upper_dark+=Dark(s[i]);
    for(unsigned i=6;i<10;++i){lower_blue+=Cyan(s[i]);lower_grey+=Grey(s[i]);}
    if(field!=2)return {};
    if(upper_dark>=3&&(lower_grey>=3||lower_blue>=3))return {Screen::Password};
    if(lower_grey>=3||lower_blue>=3)return {};
    // The domain suggestion inserts a line above Continue. Match button padding,
    // not translated text, and never choose arbitrarily between two candidates.
    constexpr std::array<unsigned,4> offsets{2,10,14,18};
    constexpr std::array<int,4> centers{366,393,420,447};
    Detection result;unsigned matches=0;
    for(unsigned candidate=0;candidate<offsets.size();++candidate) {
        unsigned blue=0,grey=0;
        for(unsigned j=0;j<4;++j){blue+=Cyan(s[offsets[candidate]+j]);grey+=Grey(s[offsets[candidate]+j]);}
        if(blue>=3||grey>=3){++matches;result={blue>=3?Screen::Email:Screen::EmailInactive,centers[candidate]/720.f};}
    }
    return matches==1?result:Detection{};
}
Screen Classify(const Samples& s){return Detect(s).screen;}
bool Flow::Start(Credentials c,Clock::time_point now) {
    if(active_||!Valid(GetProfile(Game::Epic),c)){c.Wipe();return false;}
    credentials_=std::move(c);active_=true;step_=hits_=0;index_=0;last_serial_=0;
    paused_=false;continue_y_=366.f/720;
    due_=now;deadline_=now+std::chrono::seconds(90);wait_deadline_=now+std::chrono::seconds(12);return true;
}
void Flow::Cancel(){credentials_.Wipe();active_=false;paused_=false;step_=hits_=0;index_=0;}
void Flow::Wait(unsigned next,Clock::time_point now,std::uint64_t serial,int seconds) {
    step_=next;paused_=false;hits_=0;last_serial_=serial;wait_deadline_=now+std::chrono::seconds(seconds);due_=now+std::chrono::milliseconds(300);
}
bool Flow::Confirm(Screen screen,bool email_any,std::uint64_t serial,Screen wanted) {
    if(serial<=last_serial_)return false;
    last_serial_=serial;
    const bool correct=screen==wanted||(email_any&&screen==Screen::EmailInactive);
    hits_=correct?hits_+1:0;return hits_>=2;
}
Action Flow::Tick(Clock::time_point now,bool ready,Screen screen,std::uint64_t serial,float continue_y) {
    if(!active_)return {};
    const bool waiting=step_==0||step_==4||step_==6||step_==10;
    if(!ready||now>=deadline_||(waiting&&now>=wait_deadline_)||(paused_&&now>=pause_deadline_)) {
        Cancel();return {ActionKind::Aborted,{},0,"screen_or_transport_unavailable"};
    }
    if((step_==3||step_==9)&&serial>last_serial_) {
        last_serial_=serial;
        if(screen==Screen::Unknown) {
            if(!paused_)pause_deadline_=now+std::chrono::milliseconds(2500);
            paused_=true;
        } else if(step_==9?screen!=Screen::Password:(screen!=Screen::Email&&screen!=Screen::EmailInactive)) {
            Cancel();return {ActionKind::Aborted,{},0,"screen_changed"};
        } else paused_=false;
    }
    if(paused_)return {}; // No characters while unrecognized; require a fresh matching frame.
    if(now<due_)return {};
    using namespace std::chrono;
    const auto& p=GetProfile(Game::Epic);
    switch(step_) {
    case 0: if(Confirm(screen,true,serial,Screen::Email)){step_=1;due_=now;}return {};
    case 1: ++step_;due_=now+milliseconds(220);return {ActionKind::Click,p.login,0,"epic_focus_email"};
    case 2: ++step_;due_=now+milliseconds(100);return {ActionKind::ClearField,{},0,"epic_replace_email"};
    case 3:
        if(index_<credentials_.login.size()){due_=now+milliseconds(35);return {ActionKind::Character,{},credentials_.login[index_++],"epic_type_email"};}
        index_=0;Wait(4,now,serial,10);return {};
    case 4:
        if(serial>last_serial_&&screen==Screen::Email) {
            const bool valid=continue_y==366.f/720||continue_y==393.f/720||continue_y==420.f/720||continue_y==447.f/720;
            if(!valid){hits_=0;last_serial_=serial;return {};}
            if(continue_y!=continue_y_){hits_=0;continue_y_=continue_y;}
        }
        if(Confirm(screen,false,serial,Screen::Email)){step_=5;due_=now;}return {};
    case 5: Wait(6,now,serial,20);return {ActionKind::Click,{635.f/1280,continue_y_},0,"epic_continue"};
    case 6: if(Confirm(screen,false,serial,Screen::Password)){step_=7;due_=now;}return {};
    case 7: ++step_;due_=now+milliseconds(220);return {ActionKind::Click,p.password,0,"epic_focus_password"};
    case 8: ++step_;due_=now+milliseconds(100);return {ActionKind::ClearField,{},0,"epic_clear_password"};
    case 9:
        if(index_<credentials_.password.size()){due_=now+milliseconds(35);return {ActionKind::Character,{},credentials_.password[index_++],"epic_type_password"};}
        Wait(10,now,serial,10);return {};
    case 10: if(Confirm(screen,false,serial,Screen::Password)){step_=11;due_=now;}return {};
    case 11: ++step_;due_=now+milliseconds(250);return {ActionKind::Click,p.submit,0,"epic_submit"};
    default: Cancel();return {ActionKind::Submitted,{},0,"epic_submitted"};
    }
}
} // namespace opennow::autologin::epic
