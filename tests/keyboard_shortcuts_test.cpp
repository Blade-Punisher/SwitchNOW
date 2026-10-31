#include "keyboard_shortcuts.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <vector>
using namespace opennow::input;
static ShortcutButtons Target(unsigned i) {
    ShortcutButtons b;b.minus=b.r3=true;
    if(i==0)b.escape=true;else if(i==1)b.tab=true;else if(i==2)b.alt_tab=true;else if(i==3)b.windows=true;else b.keyboard=true;
    return b;
}
int main() {
    for(unsigned i=0;i<5;++i) {
        ShortcutRouter router;auto b=Target(i);
        auto old=b;old.r3=false;
        assert(!router.Update(old,true,0).shortcut);assert(!router.Update(old,true,1000).open_keyboard);
        router.Update({},true,1100);
        // An already-held target cannot become a delayed shortcut after arming.
        assert(!router.Update(b,true,1200).shortcut);
        auto armed=router.Update(b,true,1501);assert(armed.armed&&armed.became_armed&&!armed.shortcut&&!armed.open_keyboard);
        auto prefix=b;prefix.escape=prefix.tab=prefix.alt_tab=prefix.windows=prefix.keyboard=false;
        assert(router.Update(prefix,true,1520).capture);
        auto fired=router.Update(b,true,1540);
        if(i<4)assert(fired.shortcut&&static_cast<unsigned>(*fired.shortcut)==i);else assert(fired.open_keyboard);
        assert(fired.capture);assert(!router.Update(b,true,1560).shortcut&&!router.Update(b,true,1580).open_keyboard);
        b.minus=false;
        assert(router.Update(b,true,1600).capture); // Releasing the prefix first must not leak the shoulder.
        assert(!router.Update({},true,1650).capture);
        // No commands in an overlay/auto-login context, including a held chord on re-entry.
        router.Update(Target(i),false,1700);assert(!router.Update(Target(i),false,2100).armed);
        auto entering=router.Update(Target(i),true,2150);assert(!entering.shortcut&&!entering.open_keyboard&&!entering.armed);
    }
    ShortcutRouter ambiguous;
    ShortcutButtons prefix;prefix.minus=prefix.r3=true;
    ambiguous.Update(prefix,true,0);ambiguous.Update(prefix,true,300);
    auto mixed=prefix;mixed.escape=mixed.windows=true;
    assert(!ambiguous.Update(mixed,true,320).shortcut);
    mixed.windows=false;assert(!ambiguous.Update(mixed,true,340).shortcut);
    mixed.plus=true;assert(!ambiguous.Update(mixed,true,400).shortcut); // Overlay priority.
    ShortcutRouter disarmed;disarmed.Update(prefix,true,0);assert(disarmed.Update(prefix,true,300).armed);
    disarmed.Disarm();assert(!disarmed.Update(prefix,true,400).armed);
    assert(!disarmed.Update(prefix,true,500).armed);assert(!disarmed.Update(prefix,true,700).armed);
    disarmed.Update({},true,710);disarmed.Update(prefix,true,720);assert(disarmed.Update(prefix,true,1020).armed);
    for(unsigned i=0;i<5;++i) {
        ShortcutRouter released;released.Update(prefix,true,0);assert(released.Update(prefix,true,300).became_armed);
        auto waiting=released.Update({},true,310);assert(waiting.armed&&waiting.capture);
        assert(released.RemainingMs(310)==5000);
        auto single=Target(i);single.minus=single.r3=false;
        auto command=released.Update(single,true,1000);
        if(i<4)assert(command.shortcut&&static_cast<unsigned>(*command.shortcut)==i);else assert(command.open_keyboard);
        assert(!released.Armed()&&command.capture); // one command after releasing the prefix
        assert(!released.Update(single,true,1050).shortcut&&!released.Update(single,true,1070).open_keyboard);
        assert(!released.Update({},true,1100).capture);
    }
    ShortcutRouter expiry;expiry.Update(prefix,true,0);expiry.Update(prefix,true,300);expiry.Update({},true,400);
    assert(expiry.Update({},true,5399).armed);
    auto late=Target(4);late.minus=late.r3=false;
    auto expired=expiry.Update(late,true,5400);assert(!expired.armed&&!expired.open_keyboard&&expired.capture);
    assert(!expiry.Update({},true,5410).capture);
    ShortcutRouter cancel;cancel.Update(prefix,true,0);cancel.Update(prefix,true,300);cancel.Update({},true,310);
    ShortcutButtons bcancel;bcancel.cancel=true;
    auto cancelled=cancel.Update(bcancel,true,500);assert(!cancelled.armed&&cancelled.capture);
    assert(cancel.Update(bcancel,true,510).capture);assert(!cancel.Update({},true,520).capture);
    assert(!cancel.Update(late,true,530).open_keyboard);
    ShortcutRouter held;held.Update(prefix,true,0);held.Update(prefix,true,300);
    assert(held.Update(prefix,true,20000).armed&&held.RemainingMs(20000)==-1);
    assert(held.Update(Target(1),true,20020).shortcut);
    held.Update(prefix,true,20040);assert(held.Update(Target(1),true,20100).shortcut); // held-prefix repeat by new presses
    assert(!held.Update({},true,20120).armed); // no lingering window after a held-prefix command
    // Two-button variants never trigger, with either press order.
    for(unsigned i=0;i<5;++i) {
        ShortcutRouter r;auto b=Target(i);b.minus=false;r.Update(b,true,0);
        b.r3=false;b.minus=true;assert(!r.Update(b,true,400).shortcut);
    }
    for(const auto key:{KeyboardShortcut::Escape,KeyboardShortcut::Tab,KeyboardShortcut::AltTab,KeyboardShortcut::Windows}) {
        ShortcutSequence sequence;assert(sequence.Start(key,0));assert(!sequence.Start(key,1));
        std::vector<ShortcutKeyEvent> events;std::vector<long long> times;std::set<unsigned> held;
        for(long long now=0;sequence.Active()&&now<1000;++now)if(auto event=sequence.Next(now,true)) {
            events.push_back(*event);times.push_back(now);
            if(event->pressed)held.insert(event->stroke.keycode);else assert(held.erase(event->stroke.keycode)==1);
            sequence.Commit(true,now);
        }
        assert(!sequence.Active()&&held.empty()&&!sequence.Cancelled());
        if(key==KeyboardShortcut::AltTab) {
            assert(events.size()==4&&times[1]-times[0]>=20&&times[2]-times[1]>=60&&times[3]-times[2]>=20);
            assert(events[0].stroke.keycode==0xa4&&events[0].stroke.scancode==0x38&&events[0].stroke.modifiers==4&&events[0].pressed);
            assert(events[1].stroke.keycode==9&&events[1].stroke.modifiers==4&&events[1].pressed);
            assert(events[2].stroke.keycode==9&&events[2].stroke.modifiers==4&&!events[2].pressed);
            assert(events[3].stroke.keycode==0xa4&&events[3].stroke.modifiers==0&&!events[3].pressed);
        } else {
            assert(events.size()==2&&times[1]-times[0]>=60);
            assert(events[0].pressed&&!events[1].pressed&&events[1].stroke.modifiers==0);
            if(key==KeyboardShortcut::Windows)assert(events[0].stroke.scancode==0xe05b);
            if(key==KeyboardShortcut::Escape)assert(events[0].stroke.keycode==0x1b&&events[0].stroke.scancode==1);
            if(key==KeyboardShortcut::Tab)assert(events[0].stroke.keycode==9&&events[0].stroke.scancode==0x0f);
        }
        // Backpressure: no progression until an event is accepted, including releases.
        assert(sequence.Start(key,1000));auto first=sequence.Next(1000,true);assert(first&&first->pressed);
        sequence.Commit(false,1000);assert(!sequence.Next(1001,true));
        auto retry=sequence.Next(1015,true);assert(retry&&retry->stroke.keycode==first->stroke.keycode&&retry->pressed);
        sequence.Commit(true,1015);
        sequence.Cancel(1016);
        auto release=sequence.Next(1016,true);assert(release&&!release->pressed);
        sequence.Commit(false,1016);assert(sequence.Active());
        release=sequence.Next(1031,true);assert(release&&!release->pressed);sequence.Commit(true,1031);assert(!sequence.Active());
        // Disconnect/timeout never resume a later key press after reconnect.
        assert(sequence.Start(key,2000));first=sequence.Next(2000,true);sequence.Commit(true,2000);
        assert(!sequence.Next(2001,false));assert(sequence.Releasing());
        release=sequence.Next(2100,true);assert(release&&!release->pressed);sequence.Commit(true,2100);assert(!sequence.Active());
        assert(sequence.Start(key,3000));assert(!sequence.Next(5100,true));assert(!sequence.Active());
        // Cancel after EVERY accepted event; every accepted key-down must be balanced by a key-up.
        for(size_t stop=0;stop<events.size();++stop) {
            ShortcutSequence transaction;assert(transaction.Start(key,0));held.clear();long long now=0;size_t emitted=0;
            while(emitted<stop){if(auto event=transaction.Next(now,true)){if(event->pressed)held.insert(event->stroke.keycode);else held.erase(event->stroke.keycode);transaction.Commit(true,now);++emitted;}++now;}
            transaction.Cancel(now);
            for(unsigned step=0;transaction.Active()&&step<10;++step) {
                auto event=transaction.Next(now,true);assert(event&&!event->pressed);assert(held.erase(event->stroke.keycode)==1);
                transaction.Commit(true,now);
            }
            assert(held.empty()&&!transaction.Active());
        }
    }
    std::cout<<"Keyboard guarded routing, legacy-combo rejection, 60ms hold, Alt/Win encoding, accepted-send retry, cancellation and reconnect release tests passed.\n";
}
