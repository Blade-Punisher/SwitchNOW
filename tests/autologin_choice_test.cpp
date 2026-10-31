#include "autologin_choice.hpp"
#include <cassert>
using namespace opennow::autologin;
int main(){
    Choice choice;assert(!choice.Active());assert(!choice.Update(true,false,false,false,false).owned);
    choice.Open();assert(choice.Update(true,false,true,false,false).owned&&choice.Active());
    assert(!choice.Update(true,false,false,false,false).accepted); // held A after X is not permission to type
    choice.Update(false,false,false,false,false);
    auto epic=choice.Update(true,false,false,false,false);assert(epic.accepted&&epic.target==Target::Epic&&!choice.Active());
    choice.Open();choice.Update(false,false,false,false,false);
    choice.Update(false,false,false,false,true);assert(choice.Selected()==Target::Game);
    choice.Update(false,false,false,false,true);assert(choice.Selected()==Target::Game); // no repeat on hold
    auto game=choice.Update(true,false,false,false,false);assert(game.accepted&&game.target==Target::Game);
    choice.Open();choice.Update(false,false,false,false,false);
    auto cancel=choice.Update(true,true,false,false,false);assert(cancel.cancelled&&!cancel.accepted&&!choice.Active());
    choice.Open();choice.Update(false,false,false,false,false);
    choice.Update(false,false,false,true,true);assert(choice.Selected()==Target::Epic);
    choice.Close();assert(!choice.Update(true,false,false,false,false).accepted);
}
