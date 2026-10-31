#include "game_feed_pager.hpp"
#include "game_feed_focus.hpp"
#include <cassert>
#include <iostream>
using namespace opennow;
int main(){
 GameFeedFocus focus; brls::Button old_card,new_card,other;
 focus.Remember(&old_card,"game");brls::Application::giveFocus(&old_card);
 const auto saved=focus.Capture();assert(saved=="game");focus.Clear();focus.Remember(&new_card,"game");
 assert(focus.Restore(saved) && brls::Application::getCurrentFocus()==&new_card);
 assert(!focus.Restore("removed"));brls::Application::giveFocus(&other);assert(focus.Capture().empty());
 std::size_t chosen=999;unsigned activations=0;
 auto* pager=new GameFeedPager([&](std::size_t page){chosen=page;++activations;});
 pager->Update(game_feed::Window(600,9));
 auto* next=dynamic_cast<brls::Button*>(pager->NextTarget());
 assert(next && next->text=="11");
 assert(activations==0); // DOWN only obtains the focus target, never opens it.
 assert(next->label.single_line && next->padding==8 && next->width-next->padding*2>=56);
 assert(next->shrink==0 && next->height==52);
 assert(pager->Owns(next)); next->click(next); assert(chosen==10 && activations==1);
 auto controls=pager->Buttons();assert(controls.size()<=11);
 pager->Update(game_feed::Window(225,14));
 auto wide=pager->Buttons();assert(wide.size()==10); // 1, gap, 8..14, 15
 auto* row=dynamic_cast<brls::Box*>(pager->children.front());assert(row && row->children.size()==11);
 for(auto* view:wide)assert(view->grow==1);
 pager->Update(game_feed::Window(600,9));
 brls::Button* gap=nullptr;
 for(auto* view:pager->Buttons()) {
    auto* button=dynamic_cast<brls::Button*>(view);
    if(button && button->text=="..."){gap=button;break;}
 }
 assert(gap);gap->click(gap);auto result=brls::Application::ime.result;
 result(-1);assert(activations==1);result(41);assert(activations==1);
 result(40);assert(chosen==39 && activations==2);
 pager->Update(game_feed::Window(16,1));
 auto* last=dynamic_cast<brls::Button*>(pager->NextTarget());assert(last->text=="2");
 controls=pager->Buttons();assert(controls.size()==2);
 pager->Update(game_feed::Window(600,9));gap->click(gap);
 result=brls::Application::ime.result;delete pager;result(1);assert(activations==2);
 assert(EmptyFeedMessage(true,false,false).find("Loading games")!=std::string::npos);
 std::cout<<"Production numbered pager manual-activation/next-focus/jump/bounds/lifetime tests passed.\n";
}
