#include "epic_autologin.hpp"
#include "epic_screen_probe.hpp"
#include "keyboard_shortcuts.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace opennow::autologin;
using namespace opennow::autologin::epic;
using namespace std::chrono;
// Sparse nontext colour samples from all three supplied 1280x720 screenshots.
static const Samples email{{{23,22,27},{23,22,27},{28,199,255},{28,199,255},{28,199,255},{28,199,255},{8,9,13},{8,9,13},{8,9,13},{8,9,13},{12,6,16},{10,7,14},{12,7,14},{6,9,16},{8,9,13},{8,9,13},{8,9,13},{8,9,13},{8,9,13},{8,9,13},{8,9,13},{8,9,13}}};
static const Samples password{{{8,9,13},{8,9,13},{23,22,27},{23,22,27},{23,22,27},{23,22,27},{60,59,64},{60,59,64},{60,59,64},{60,59,64},{8,9,13},{6,10,13},{8,9,13},{8,9,13},{4,8,11},{6,10,11},{8,9,13},{8,9,13},{40,193,251},{39,193,255},{8,9,13},{8,9,13}}};
static const Samples suggestion{{{23,22,27},{23,22,27},{5,10,16},{5,10,16},{6,9,18},{6,9,16},{8,9,13},{74,73,78},{9,10,14},{8,9,13},{28,199,255},{28,199,255},{28,199,255},{28,199,255},{10,11,15},{13,10,17},{12,5,12},{7,9,8},{8,9,13},{8,9,13},{8,9,13},{8,9,13}}};
using EpicFlow=opennow::autologin::epic::Flow;
static const Credentials credentials{"test@example.invalid","Fake-Test!7"};
static std::vector<Action> Replay(int loss_at=-1,bool change_page=true,bool ready_to_submit=true) {
    EpicFlow flow;auto now=EpicFlow::Clock::time_point{}+seconds(1);assert(flow.Start(credentials,now));
    std::uint64_t serial=0;Screen screen=Screen::Unknown;std::vector<Action> result;
    auto continued=EpicFlow::Clock::time_point{};bool pass=false;
    for(unsigned frame=0;flow.Active()&&frame<6000;++frame,now+=milliseconds(20)) {
        if(flow.NeedsProbe()&&frame%25==0){++serial;screen=pass?Screen::Password:Screen::Email;}
        if(change_page&&continued.time_since_epoch().count()&&now-continued>=milliseconds(1500))pass=true;
        if(!ready_to_submit&&!result.empty()&&std::string(result.back().stage)=="epic_type_password")screen=Screen::Unknown;
        const auto action=flow.Tick(now,loss_at<0||int(result.size())!=loss_at,screen,serial);
        if(action.kind!=ActionKind::None){
            result.push_back(action);
            if(std::string(action.stage)=="epic_continue")continued=now;
            if(std::string(action.stage)=="epic_type_password")assert(pass);
        }
    }
    assert(!flow.Active());return result;
}

static void SuggestionRegression(bool disappear) {
    const Credentials account{"test@example.com","Fake-Test!7"};
    EpicFlow flow;auto now=EpicFlow::Clock::time_point{}+seconds(1);assert(flow.Start(account,now));
    std::string typed;bool shifted=false,continued=false;std::uint64_t serial=0;Detection d;
    for(unsigned frame=0;frame<1000&&!continued;++frame,now+=milliseconds(20)) {
        if(typed.find('.')!=std::string::npos)shifted=true;
        if(frame%25==0){d=Detect(shifted&&!(disappear&&typed==account.login)?suggestion:email);++serial;}
        const auto a=flow.Tick(now,true,d.screen,serial,d.continue_y);
        assert(a.kind!=ActionKind::Aborted);
        if(a.kind==ActionKind::Character)typed+=a.character;
        if(std::string(a.stage)=="epic_continue") {
            assert(typed==account.login);assert(a.point.y==(disappear?366.f/720:393.f/720));continued=true;
        }
    }
    assert(shifted&&continued);
}
static void StableButtonRegression() {
    EpicFlow f;auto now=EpicFlow::Clock::time_point{}+seconds(1);assert(f.Start(credentials,now));
    std::uint64_t serial=0;std::string typed;
    while(typed.size()<credentials.login.size()) {now+=milliseconds(40);const auto a=f.Tick(now,true,Screen::Email,++serial);if(a.kind==ActionKind::Character)typed+=a.character;}
    now+=milliseconds(40);f.Tick(now,true,Screen::Email,++serial); // enter post-email verification
    now+=milliseconds(400);assert(f.Tick(now,true,Screen::Email,++serial,393.f/720).kind==ActionKind::None);
    now+=milliseconds(500);assert(f.Tick(now,true,Screen::Email,++serial,366.f/720).kind==ActionKind::None); // position changed: only one hit
    now+=milliseconds(40);assert(f.Tick(now,true,Screen::Email,serial,366.f/720).kind==ActionKind::None); // stale sample
    now+=milliseconds(500);assert(f.Tick(now,true,Screen::Email,++serial,366.f/720).kind==ActionKind::None);
    now+=milliseconds(40);const auto a=f.Tick(now,true,Screen::Email,serial,366.f/720);assert(std::string(a.stage)=="epic_continue"&&a.point.y==366.f/720);
}
static void PauseRegression(bool recover,bool wrong_page=false) {
    EpicFlow f;auto now=EpicFlow::Clock::time_point{}+seconds(1);assert(f.Start(credentials,now));
    std::uint64_t serial=0;Action a;
    do {now+=milliseconds(20);a=f.Tick(now,true,Screen::Email,++serial);}while(a.kind!=ActionKind::Character);
    now+=milliseconds(40);
    if(wrong_page){assert(f.Tick(now,true,Screen::Password,++serial).kind==ActionKind::Aborted);return;}
    assert(f.Tick(now,true,Screen::Unknown,++serial).kind==ActionKind::None);
    // Neither stale known data nor repeated unknown samples may resume printing.
    for(unsigned i=0;i<30;++i){now+=milliseconds(20);assert(f.Tick(now,true,i%2?Screen::Unknown:Screen::Email,serial).kind==ActionKind::None);}
    if(recover){now+=milliseconds(20);a=f.Tick(now,true,Screen::Email,++serial);assert(a.kind==ActionKind::Character&&a.character==credentials.login[1]);}
    else {now+=seconds(3);assert(f.Tick(now,true,Screen::Unknown,++serial).kind==ActionKind::Aborted);}
}

int main() {
    SuggestionRegression(false);SuggestionRegression(true);StableButtonRegression();PauseRegression(true);PauseRegression(false);PauseRegression(false,true);
    assert(Detect(suggestion).screen==Screen::Email&&Detect(suggestion).continue_y==393.f/720);
    for(unsigned slot:{14u,18u}) {auto shifted=email;for(unsigned i=2;i<6;++i)shifted[i]={8,9,13};for(unsigned j=0;j<4;++j)shifted[slot+j]={28,199,255};
        assert(Detect(shifted).screen==Screen::Email);assert(Detect(shifted).continue_y==(slot==14?420.f/720:447.f/720));}
    auto ambiguous=suggestion;for(unsigned i=2;i<6;++i)ambiguous[i]={28,199,255};assert(Classify(ambiguous)==Screen::Unknown);
    assert(Eligible("Cyberpunk 2077","EPIC"));assert(Eligible("Any other game","Epic Games Store"));
    for(auto name:{"Fortnite","FORTNITE®","Fortnite: Battle Royale"})assert(!Eligible(name,"EPIC"));
    assert(!Eligible("堡垒之夜","EPIC"));assert(!Eligible("Фортнайт","EPIC"));
    assert(Eligible("另一款游戏","EPIC")); // non-Latin game names still qualify for the selected store
    for(auto store:{"STEAM","UPLAY","","Steam / EPIC","unknown"})assert(!Eligible("Cyberpunk 2077",store));
    assert(!FindProfile("Epic Games"));
    assert(Classify(email)==Screen::Email&&Classify(password)==Screen::Password);
    for(unsigned i=2;i<6;++i)assert(kSampleX[i]<424||kSampleX[i]>850); // independent of typed text/dots
    Samples inactive=email;for(unsigned i=2;i<6;++i)inactive[i]={60,59,64};assert(Classify(inactive)==Screen::EmailInactive);
    Samples filled=password;for(unsigned i=6;i<10;++i)filled[i]={28,199,255};assert(Classify(filled)==Screen::Password);
    for(auto sample:{email,password}) {
        const auto expected=Classify(sample);
        for(int noise:{-8,8}){auto altered=sample;for(auto& p:altered){p.r=std::clamp(int(p.r)+noise,0,255);p.g=std::clamp(int(p.g)+noise,0,255);p.b=std::clamp(int(p.b)+noise,0,255);}assert(Classify(altered)==expected);}
        for(auto& p:sample){p.r/=2;p.g/=2;p.b/=2;}assert(Classify(sample)==Screen::Unknown); // dimmed dialog background
    }
    assert(Classify(Samples{})==Screen::Unknown);
    Samples white;white.fill({255,255,255});assert(Classify(white)==Screen::Unknown);
    const auto actions=Replay();assert(actions.back().kind==ActionKind::Submitted);
    unsigned clears=0,clicks=0;std::string login,pass;
    for(const auto& a:actions) {
        clears+=a.kind==ActionKind::ClearField;clicks+=a.kind==ActionKind::Click;
        if(a.kind==ActionKind::Character)(std::string(a.stage)=="epic_type_email"?login:pass)+=a.character;
        if(std::string(a.stage)=="epic_continue")assert(a.point.y>342.f/720&&a.point.y<390.f/720);
        if(std::string(a.stage)=="epic_submit")assert(a.point.y>516.f/720&&a.point.y<564.f/720);
    }
    assert(login==credentials.login&&pass==credentials.password&&clears==2&&clicks==4);
    const auto stalled=Replay(-1,false);assert(stalled.back().kind==ActionKind::Aborted);
    for(const auto& a:stalled)assert(std::string(a.stage)!="epic_type_password");
    const auto changed=Replay(-1,true,false);assert(changed.back().kind==ActionKind::Aborted);
    for(const auto& a:changed)assert(std::string(a.stage)!="epic_submit");
    for(unsigned i=0;i<actions.size()-1;++i){auto lost=Replay(int(i));assert(lost.back().kind==ActionKind::Aborted);}
    EpicFlow unknown;auto now=EpicFlow::Clock::time_point{}+seconds(1);assert(unknown.Start(credentials,now));
    assert(unknown.Tick(now,true,Screen::Unknown,1).kind==ActionKind::None);
    assert(unknown.Tick(now+seconds(13),true,Screen::Unknown,2).kind==ActionKind::Aborted);
    EpicFlow frozen;assert(frozen.Start(credentials,now));
    for(unsigned i=0;i<100;++i)assert(frozen.Tick(now+milliseconds(i*20),true,Screen::Email,1).kind==ActionKind::None);
    frozen.Cancel();assert(!frozen.Active());
    assert(!unknown.Start({"no-email","password"},now));
    EpicFlow blank;assert(blank.Start(credentials,now));
    blank.Tick(now,true,Screen::EmailInactive,1);blank.Tick(now+milliseconds(500),true,Screen::EmailInactive,2);
    assert(blank.Tick(now+milliseconds(520),true,Screen::EmailInactive,2).kind==ActionKind::Click);blank.Cancel();
    auto job=std::make_shared<Probe>();RequestProbe(job,now);assert(job->serial==1&&!job->pending);
    RequestProbe(job,now+milliseconds(50));assert(job->serial==1);
    RequestProbe(job,now+milliseconds(500));assert(job->serial==2);
    job->active=false;RequestProbe(job,now+seconds(2));assert(job->serial==2);
    opennow::autologin::Flow generic;assert(!generic.Start(GetProfile(Game::Epic),credentials,now));
    const auto& p=GetProfile(Game::Epic);
    assert(CredentialsPath(p)=="sdmc:/switch/SwitchNOW/Epic/Epic.txt");
    auto home=std::filesystem::temp_directory_path()/"switchnow-epic-auth-test";std::filesystem::create_directories(Directory(p,home.string()));
    assert(Save(p,credentials,home.string()));assert(Load(p,home.string()).login==credentials.login);assert(Clear(p,home.string()));std::filesystem::remove_all(home);
    using namespace opennow::input;
    ShortcutRouter router;ShortcutButtons prefix;prefix.minus=prefix.r3=true;
    router.Update(prefix,true,0);router.Update(prefix,true,300);router.Update({},true,310);
    ShortcutButtons x;x.autologin=true;assert(router.Update(x,true,400).start_autologin);assert(!router.Update(x,true,420).start_autologin);
    std::cout<<"R71 full email suffix and shifted/disappearing suggestion, transient unknown pause/recovery, strict page guard; Epic eligibility/Fortnite exclusion, real screenshot samples, blank/filled email replacement, two-stage guards, stale-frame/timeout/loss abort, isolated storage and guarded X tests passed.\n";
}
