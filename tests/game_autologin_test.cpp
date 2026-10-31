#include "game_autologin.hpp"
#include "keyboard_input_policy.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
using namespace opennow::autologin;
using namespace std::chrono;
int main() {
    assert(FindProfile("Genshin Impact")->game==Game::Genshin);
    assert(FindProfile("Honkai: Star Rail")->game==Game::Honkai);
    assert(FindProfile("Wuthering Waves")->game==Game::Wuwa);
    assert(FindProfile("Zenless Zone Zero")->game==Game::Zzz);
    assert(FindProfile("NTE: Neverness to Everness")->game==Game::Nte);
    assert(FindProfile("GENSHIN IMPACT®")->game==Game::Genshin);
    assert(!FindProfile("Honkai Impact 3rd"));assert(!FindProfile("Genshin Impact Guide"));assert(!FindProfile("Fortnite"));
    std::set<std::string> paths;
    const auto root=std::filesystem::temp_directory_path()/"switchnow-r67-autologin-test";
    std::filesystem::create_directories(root);
    const auto cwd=std::filesystem::current_path();std::filesystem::current_path(root);
    for(const auto game:{Game::Genshin,Game::Honkai,Game::Wuwa,Game::Zzz}) {
        const auto& p=GetProfile(game);
        assert(paths.insert(CredentialsPath(p)).second);
        assert(CredentialsPath(p)==Directory(p)+"/"+p.folder+".txt");
        const Credentials c{"test@example.invalid"," PaSs=word! # "};
        assert(Valid(p,c));assert(Parse(p,Serialize(p,c)).password==c.password);
        assert(Parse(p,"username=User42\npassword=secret\n").login==(p.email_only?"":"User42"));
        assert(Parse(p,"email=test@example.invalid\r\npassword=a=b\r\n").password=="a=b");
        assert(Parse(p,"test@example.invalid\nsecret\n").password=="secret");
        assert(!Valid(p,{"test@example.invalid","bad\npassword"}));
        assert(!Valid(p,{"test@example.invalid",std::string("x\0y",3)}));
        assert(!Valid(p,{"test@example.invalid","пароль"}));
        assert(!Valid(p,{std::string(255,'a'),"secret"}));
        assert(!Valid(p,{"test@example.invalid",std::string(257,'a')}));
        assert(Parse(p,std::string(4097,'a')).login.empty());
        std::filesystem::create_directories(Directory(p,"test-home"));
        assert(Save(p,c,"test-home"));auto loaded=Load(p,"test-home");assert(loaded.login==c.login&&loaded.password==c.password);loaded.Wipe();
        assert(!Save(p,{"", ""},"test-home"));assert(Load(p,"test-home").password==c.password);
        std::ofstream(CredentialsPath(p,"test-home")+".tmp")<<"test";
        std::ofstream(CredentialsPath(p,"test-home")+".bak")<<"test";
        assert(Clear(p,"test-home"));assert(Clear(p,"test-home"));assert(Load(p,"test-home").login.empty());
        assert(!std::filesystem::exists(CredentialsPath(p,"test-home")+".tmp"));
        assert(!std::filesystem::exists(CredentialsPath(p,"test-home")+".bak"));
        // Replay at uneven UI intervals: no burst catch-up, ordering and all bytes preserved.
        Flow flow;auto now=Flow::Clock::time_point{}+seconds(1);
        assert(flow.Start(p,c,now));assert(!flow.Start(p,{"other@example.invalid","other"},now));
        std::vector<Action> actions;std::string login,password;bool typing_password=false;
        for(unsigned frame=0;flow.Active()&&frame<4000;++frame) {
            auto action=flow.Tick(now,true);now+=milliseconds(frame%11==0?53:16);
            if(action.kind==ActionKind::None)continue;
            if(std::string(action.stage)=="focus_password")typing_password=true;
            if(action.kind==ActionKind::Character) {
                opennow::input::KeyboardStroke stroke{};
                assert(opennow::input::MapAsciiKey(action.character,stroke));
                (typing_password?password:login)+=action.character;
            }
            actions.push_back(action);
        }
        assert(!flow.Active());assert(login==c.login&&password==c.password);
        assert(actions.front().kind==ActionKind::Click);
        assert(actions.back().kind==ActionKind::Submitted);
        std::vector<Point> clicks;
        unsigned clears=0;for(const auto& action:actions){if(action.kind==ActionKind::Click)clicks.push_back(action.point);if(action.kind==ActionKind::ClearField)++clears;}
        assert(clicks.size()==3&&clears==2);
        // Targets must fall inside the field/button rectangles in the reference captures.
        auto inside=[](Point pt,int x,int y,int w,int h){const float px=pt.x*1280,py=pt.y*720;return px>x&&px<x+w&&py>y&&py<y+h;};
        if(game==Game::Genshin||game==Game::Honkai){assert(inside(clicks[0],456,272,368,44));assert(inside(clicks[1],456,328,368,44));assert(inside(clicks[2],456,424,368,44));}
        if(game==Game::Wuwa){assert(inside(clicks[0],312,190,655,88));assert(inside(clicks[1],312,294,655,87));assert(inside(clicks[2],312,470,656,88));}
        if(game==Game::Zzz){assert(inside(clicks[0],400,265,480,50));assert(inside(clicks[1],400,332,480,50));assert(inside(clicks[2],400,399,480,48));}
        // Every possible cancellation / transport loss point must prevent further typing/submission.
        for(unsigned stop=0;stop<actions.size();++stop) {
            Flow interrupted;now=Flow::Clock::time_point{}+seconds(1);assert(interrupted.Start(p,c,now));
            unsigned emitted=0;
            while(emitted<stop){auto a=interrupted.Tick(now,true);if(a.kind!=ActionKind::None)++emitted;now+=milliseconds(40);}
            assert(interrupted.Tick(now,false).kind==ActionKind::Aborted);
            assert(!interrupted.Active());assert(interrupted.Tick(now+seconds(1),true).kind==ActionKind::None);
            assert(interrupted.Start(p,c,now));interrupted.Cancel();assert(interrupted.Tick(now,true).kind==ActionKind::None);
        }
        assert(flow.Start(p,c,now));assert(flow.Tick(now+seconds(61),true).kind==ActionKind::Aborted);
        assert(!flow.Active());
        assert(flow.Start(p,c,now));flow.Cancel();
    }
    assert(CredentialsPath(GetProfile(Game::Nte))=="sdmc:/switch/SwitchNOW/nte/nte.txt");
    const auto legacy=Parse(GetProfile(Game::Nte),"email=test@example.invalid\npassword= secret \n");
    assert(legacy.login=="test@example.invalid"&&legacy.password=="secret");
    assert(!Flow{}.Start(GetProfile(Game::Nte),{"test@example.invalid","secret"},Flow::Clock::now()));
    std::filesystem::current_path(cwd);std::filesystem::remove_all(root);
    std::cout<<"Production four-game auto-login profiles, exact screen targets, storage isolation, keyboard byte order, cancel/loss/timeout and NTE compatibility passed.\n";
}
