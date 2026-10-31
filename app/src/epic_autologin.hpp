#pragma once
#include "game_autologin.hpp"
#include <array>
#include <cstdint>
namespace opennow::autologin::epic {
bool Eligible(const std::string& title,const std::string& selected_store);
struct Color { unsigned char r=0,g=0,b=0; };
using Samples=std::array<Color,22>;
// Sample input padding, never the email/password glyphs or the eye icon.
inline constexpr std::array<int,22> kSampleX{410,860,410,416,856,860,420,450,820,850,410,416,856,860,410,416,856,860,410,416,856,860};
inline constexpr std::array<int,6> kRowY{298,366,540,393,420,447};
enum class Screen { Unknown,EmailInactive,Email,Password };
struct Detection { Screen screen=Screen::Unknown; float continue_y=366.f/720; };
Detection Detect(const Samples&);
Screen Classify(const Samples&);
class Flow {
public:
    using Clock=std::chrono::steady_clock;
    Flow()=default;
    Flow(const Flow&)=delete;
    Flow& operator=(const Flow&)=delete;
    ~Flow(){Cancel();}
    bool Start(Credentials,Clock::time_point);
    Action Tick(Clock::time_point,bool ready,Screen,std::uint64_t serial,float continue_y=366.f/720);
    void Cancel();
    bool Active() const {return active_;}
    bool NeedsProbe() const {return active_&&(step_==0||step_==3||step_==4||step_==6||step_==9||step_==10);}
private:
    bool Confirm(Screen,bool email_any,std::uint64_t serial,Screen wanted);
    void Wait(unsigned,Clock::time_point,std::uint64_t,int seconds);
    bool active_=false;
    Credentials credentials_;
    unsigned step_=0,hits_=0;
    size_t index_=0;
    std::uint64_t last_serial_=0;
    float continue_y_=366.f/720;
    bool paused_=false;
    Clock::time_point due_{},deadline_{},wait_deadline_{},pause_deadline_{};
};
} // namespace opennow::autologin::epic
