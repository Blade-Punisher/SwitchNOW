#include "game_autologin.hpp"
#include "nte_credentials.hpp"
#include "atomic_file_replace.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace opennow::autologin {
namespace {
// Centres of the input/button rectangles in the supplied 1280x720 captures.
constexpr std::array<Profile, 6> profiles{{
    {Game::Nte,"NTE","nte",true,{.55f,.35f},{.55f,.45f},{}},
    {Game::Genshin,"Genshin Impact","Genshin",false,{640.f/1280,294.f/720},{640.f/1280,350.f/720},{640.f/1280,446.f/720}},
    {Game::Honkai,"Honkai: Star Rail","Honkai",false,{640.f/1280,294.f/720},{640.f/1280,350.f/720},{640.f/1280,446.f/720}},
    {Game::Wuwa,"Wuthering Waves","wuwa",true,{640.f/1280,234.f/720},{640.f/1280,338.f/720},{640.f/1280,514.f/720}},
    {Game::Zzz,"Zenless Zone Zero","zzz",false,{640.f/1280,290.f/720},{640.f/1280,357.f/720},{640.f/1280,423.f/720}},
    {Game::Epic,"Epic Games","Epic",true,{635.f/1280,298.f/720},{635.f/1280,363.f/720},{635.f/1280,540.f/720}}
}};
std::string Canonical(const std::string& text) {
    std::string out;
    for(unsigned char c:text)if(c<128&&std::isalnum(c))out+=static_cast<char>(std::tolower(c));
    return out;
}
std::string Trim(std::string s) {
    auto space=[](unsigned char c){return std::isspace(c)!=0;};
    while(!s.empty()&&space(s.back()))s.pop_back();
    auto first=std::find_if_not(s.begin(),s.end(),space);s.erase(s.begin(),first);return s;
}
bool Printable(const std::string& text,size_t limit) {
    return !text.empty()&&text.size()<=limit&&std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32&&c<=126;});
}
void EnsureDirectory(const Profile& p,const std::string& home) {
#ifdef __SWITCH__
    mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/SwitchNOW",0777);
    mkdir(Directory(p,home).c_str(),0777);
#else
    (void)p;(void)home;
#endif
}
}
const Profile& GetProfile(Game game) {
    for(const auto& p:profiles)if(p.game==game)return p;
    return profiles.front();
}
const Profile* FindProfile(const std::string& title) {
    if(IsNevernessToEverness(title))return &profiles.front();
    const auto name=Canonical(title);
    for(const auto& p:profiles)if(p.game!=Game::Nte&&p.game!=Game::Epic&&name==Canonical(p.title))return &p;
    return nullptr;
}
void Credentials::Wipe() {
    std::fill(login.begin(),login.end(),'\0');std::fill(password.begin(),password.end(),'\0');
    login.clear();password.clear();
}
bool Valid(const Profile& p,const Credentials& c) {
    return Printable(c.login,254)&&Printable(c.password,256)&&
        (!p.email_only||(c.login.find('@')!=std::string::npos));
}
Credentials Parse(const Profile& p,const std::string& text) {
    if(text.size()>4096)return {};
    if(p.game==Game::Nte) {
        const auto legacy=ParseNteCredentials(text);
        return {legacy.email,legacy.password};
    }
    Credentials c;std::string positional[2];unsigned count=0;
    std::istringstream input(text);std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto trimmed=Trim(line);
        if(trimmed.empty()||trimmed.front()=='#')continue;
        const auto equal=line.find('=');
        const auto key=equal==std::string::npos?std::string{}:Canonical(Trim(line.substr(0,equal)));
        if(key=="email"||key=="login"||key=="username")c.login=Trim(line.substr(equal+1));
        else if(key=="password")c.password=line.substr(equal+1);
        else if(equal==std::string::npos&&count<2)positional[count++]=line;
    }
    if(c.login.empty()&&count)c.login=Trim(positional[0]);
    if(c.password.empty()&&count>1)c.password=positional[1];
    if(!Valid(p,c)){c.Wipe();return {};}
    return c;
}
std::string Serialize(const Profile& p,const Credentials& c) {
    if(!Valid(p,c))return {};
    return "email="+c.login+"\npassword="+c.password+"\n";
}
std::string Directory(const Profile& p,const std::string& home) {return home+"/"+p.folder;}
std::string CredentialsPath(const Profile& p,const std::string& home) {return Directory(p,home)+"/"+p.folder+".txt";}
Credentials Load(const Profile& p,const std::string& home) {
    std::ifstream f(CredentialsPath(p,home),std::ios::binary|std::ios::ate);
    if(!f||f.tellg()<0||f.tellg()>4096)return {};
    std::string text(static_cast<size_t>(f.tellg()),'\0');f.seekg(0);
    if(!f.read(text.data(),static_cast<std::streamsize>(text.size())))return {};
    auto c=Parse(p,text);std::fill(text.begin(),text.end(),'\0');return c;
}
bool Save(const Profile& p,const Credentials& c,const std::string& home) {
    auto text=Serialize(p,c);if(text.empty())return false;
    EnsureDirectory(p,home);const auto path=CredentialsPath(p,home),temporary=path+".tmp";
    std::ofstream f(temporary,std::ios::binary|std::ios::trunc);
    if(!f){std::fill(text.begin(),text.end(),'\0');return false;}
    f.write(text.data(),static_cast<std::streamsize>(text.size()));f.flush();const bool ok=f.good();f.close();
    std::fill(text.begin(),text.end(),'\0');
    if(!ok){std::remove(temporary.c_str());return false;}
    return storage::ReplaceWithTemporaryFile(temporary,path);
}
bool Clear(const Profile& p,const std::string& home) {
    const auto path=CredentialsPath(p,home);
    const auto remove=[](const std::string& name){errno=0;return std::remove(name.c_str())==0||errno==ENOENT;};
    const bool main=remove(path),temporary=remove(path+".tmp"),backup=remove(path+".bak");
    return main&&temporary&&backup;
}
Flow::~Flow(){Cancel();}
bool Flow::Start(const Profile& p,Credentials c,Clock::time_point now) {
    if(Active()||p.game==Game::Nte||p.game==Game::Epic||!Valid(p,c)){c.Wipe();return false;}
    // Store a stable built-in profile, never a caller's stack address.
    profile_=&GetProfile(p.game);credentials_=std::move(c);step_=0;index_=0;due_=now;deadline_=now+std::chrono::seconds(60);return true;
}
void Flow::Cancel(){credentials_.Wipe();profile_=nullptr;step_=0;index_=0;}
Action Flow::Tick(Clock::time_point now,bool input_ready) {
    if(!Active())return {};
    if(!input_ready||now>=deadline_){Cancel();return {ActionKind::Aborted,{},0,"transport_or_timeout"};}
    if(now<due_)return {};
    using namespace std::chrono;
    switch(step_) {
    case 0: ++step_;due_=now+milliseconds(220);return {ActionKind::Click,profile_->login,0,"focus_login"};
    case 1: ++step_;due_=now+milliseconds(100);return {ActionKind::ClearField,{},0,"clear_login"};
    case 2:
        if(index_<credentials_.login.size()){due_=now+milliseconds(35);return {ActionKind::Character,{},credentials_.login[index_++],"type_login"};}
        ++step_;index_=0;due_=now+milliseconds(300);return {};
    case 3: ++step_;due_=now+milliseconds(220);return {ActionKind::Click,profile_->password,0,"focus_password"};
    case 4: ++step_;due_=now+milliseconds(100);return {ActionKind::ClearField,{},0,"clear_password"};
    case 5:
        if(index_<credentials_.password.size()){due_=now+milliseconds(35);return {ActionKind::Character,{},credentials_.password[index_++],"type_password"};}
        ++step_;due_=now+milliseconds(300);return {};
    case 6: ++step_;due_=now+milliseconds(250);return {ActionKind::Click,profile_->submit,0,"submit_login"};
    default: Cancel();return {ActionKind::Submitted,{},0,"submitted"};
    }
}
} // namespace opennow::autologin
