#pragma once
namespace opennow::autologin {
enum class Target {Epic,Game};
struct ChoiceDecision {bool owned=false,accepted=false,cancelled=false;Target target=Target::Epic;};
class Choice {
public:
    void Open(){active_=true;ready_=false;selected_=Target::Epic;up_=down_=false;}
    void Close(){active_=false;ready_=false;}
    bool Active()const{return active_;}
    Target Selected()const{return selected_;}
    ChoiceDecision Update(bool a,bool b,bool x,bool up,bool down) {
        if(!active_)return {};
        ChoiceDecision result;result.owned=true;
        if(!ready_){if(!a&&!b&&!x)ready_=true;return result;}
        if(up!=down&&((up&&!up_)||(down&&!down_)))selected_=selected_==Target::Epic?Target::Game:Target::Epic;
        up_=up;down_=down;
        if(a||b){result.accepted=!b;result.cancelled=b;result.target=selected_;Close();}
        return result;
    }
private:
    bool active_=false,ready_=false,up_=false,down_=false;
    Target selected_=Target::Epic;
};
}
