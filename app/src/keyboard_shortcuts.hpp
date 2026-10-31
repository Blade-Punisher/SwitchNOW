#pragma once
#include "keyboard_input_policy.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace opennow::input {
struct ShortcutButtons {
    bool minus=false,plus=false,r3=false,escape=false,tab=false,alt_tab=false,windows=false,keyboard=false,cancel=false,autologin=false;
};
struct ShortcutDecision {
    std::optional<KeyboardShortcut> shortcut;
    bool capture=false;
    bool open_keyboard=false,start_autologin=false,armed=false,became_armed=false;
};
class ShortcutRouter {
public:
    void Disarm() { since_=-1;armed_=false;deadline_=0;blocked_=true; }
    bool Armed() const { return armed_; }
    // -1 means the prefix is still held, so there is no selection timeout yet.
    std::int64_t RemainingMs(std::int64_t now_ms) const {
        if(!armed_)return 0;
        return deadline_==0||prefix_?-1:(deadline_>now_ms?deadline_-now_ms:0);
    }
    ShortcutDecision Update(ShortcutButtons b,bool enabled,std::int64_t now_ms) {
        const unsigned bits=(b.escape?1u:0u)|(b.tab?2u:0u)|(b.alt_tab?4u:0u)|(b.windows?8u:0u)|(b.keyboard?16u:0u)|(b.autologin?32u:0u);
        ShortcutDecision out;
        const bool single=bits && !(bits&(bits-1));
        const bool prefix=b.minus&&b.r3&&!b.plus;
        const bool was_armed=armed_;
        if(!b.minus&&!b.r3)blocked_=false;
        if(prefix)captured_=true;
        if(!enabled||b.plus||(b.cancel&&(armed_||captured_))) {
            Disarm();
        } else if(armed_) {
            if(!prefix&&prefix_) {
                if(used_)Disarm();
                else deadline_=now_ms+5000;
            }
            if(armed_&&!prefix&&deadline_>0&&now_ms>=deadline_)Disarm();
        } else if(prefix&&!blocked_) {
            if(since_<0)since_=now_ms;
            if(now_ms-since_>=300){armed_=true;used_=false;deadline_=0;}
        } else since_=-1;
        // Commands require a NEW target press AFTER arming. Holding all three
        // buttons before the delay expires never turns into a delayed command.
        if(enabled&&!b.plus&&!b.cancel&&was_armed&&armed_&&single&&!(previous_&bits)) {
            if(bits==1)out.shortcut=KeyboardShortcut::Escape;
            else if(bits==2)out.shortcut=KeyboardShortcut::Tab;
            else if(bits==4)out.shortcut=KeyboardShortcut::AltTab;
            else if(bits==8)out.shortcut=KeyboardShortcut::Windows;
            else if(bits==16)out.open_keyboard=true;
            else out.start_autologin=true;
            if(prefix)used_=true;
            else Disarm(); // Released-prefix mode is a single command, not a sticky keyboard layer.
        }
        if(!armed_&&!b.minus&&!b.r3&&!bits&&!b.cancel)captured_=false;
        out.capture=captured_||armed_;out.armed=armed_;out.became_armed=armed_&&!was_armed;
        previous_=bits;prefix_=prefix;return out;
    }
private:
    unsigned previous_=0;
    bool captured_=false,armed_=false,prefix_=false,used_=false,blocked_=false;
    std::int64_t since_=-1,deadline_=0;
};
struct ShortcutKeyEvent {KeyboardStroke stroke;bool pressed=false;};
// Small nonblocking transaction. Progress is committed only after SCTP accepts
// an event. Failed sends cannot skip a modifier/key release.
class ShortcutSequence {
public:
    bool Start(KeyboardShortcut key,std::int64_t now_ms) {
        if(Active())return false;
        count_=0;index_=0;held_count_=0;cancelled_=false;
        const auto stroke=MapKeyboardShortcut(key);
        if(key==KeyboardShortcut::AltTab) {
            Add({0xa4,0x38,4},true,20);Add(stroke,true,60);
            Add(stroke,false,20);Add({0xa4,0x38,0},false,0);
        } else {
            Add(stroke,true,60);auto release=stroke;release.modifiers=0;Add(release,false,0);
        }
        due_=now_ms;deadline_=now_ms+2000;return true;
    }
    bool Active() const {return index_<count_;}
    bool Cancelled() const {return cancelled_;}
    bool Releasing() const {return cancelled_&&Active();}
    void Cancel(std::int64_t now_ms) {
        if(cancelled_)return;
        cancelled_=true;index_=count_=0;due_=now_ms;
        unsigned modifiers=0;
        for(size_t i=0;i<held_count_;++i)modifiers|=Modifier(held_[i]);
        for(size_t i=held_count_;i>0;--i) {
            auto key=held_[i-1];modifiers&=~Modifier(key);key.modifiers=static_cast<std::uint16_t>(modifiers);
            Add(key,false,0);
        }
    }
    std::optional<ShortcutKeyEvent> Next(std::int64_t now_ms,bool ready) {
        if(!Active())return {};
        if(!cancelled_&&(!ready||now_ms>=deadline_))Cancel(now_ms);
        if(!Active()||!ready||now_ms<due_)return {};
        return events_[index_].event;
    }
    void Commit(bool accepted,std::int64_t now_ms) {
        if(!Active())return;
        if(!accepted){due_=now_ms+15;return;}
        const auto& item=events_[index_];
        if(item.event.pressed) {
            if(held_count_<held_.size())held_[held_count_++]=item.event.stroke;
        } else {
            for(size_t i=0;i<held_count_;++i)if(held_[i].keycode==item.event.stroke.keycode) {
                for(size_t j=i+1;j<held_count_;++j)held_[j-1]=held_[j];
                --held_count_;break;
            }
        }
        due_=now_ms+item.delay_after_ms;++index_;
    }
private:
    struct Step {ShortcutKeyEvent event;int delay_after_ms;};
    void Add(KeyboardStroke key,bool pressed,int delay) {events_[count_++]={{key,pressed},delay};}
    static unsigned Modifier(KeyboardStroke key) {return key.keycode==0xa4?4u:key.keycode==0x5b?8u:0u;}
    std::array<Step,4> events_{};
    std::array<KeyboardStroke,2> held_{};
    size_t count_=0,index_=0,held_count_=0;
    std::int64_t due_=0,deadline_=0;
    bool cancelled_=false;
};
inline const char* ShortcutName(KeyboardShortcut key) {
    switch(key) {
    case KeyboardShortcut::Escape:return "Esc";
    case KeyboardShortcut::Tab:return "Tab";
    case KeyboardShortcut::AltTab:return "Alt+Tab";
    case KeyboardShortcut::Windows:return "Win";
    }
    return "unknown";
}
} // namespace opennow::input
