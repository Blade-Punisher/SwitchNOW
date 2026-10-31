#include "gfn/library_ownership.hpp"
#include "owned_game_policy.hpp"
#include "app_state.hpp"
#include <jansson.h>
#include <cassert>
#include <iostream>
using namespace opennow;
template<class F>void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
 auto body=gfn::ownership::RequestBody("123456");
 auto* root=json_loads(body.c_str(),0,nullptr);assert(root);
 auto* vars=json_object_get(root,"variables");
 assert(std::string(json_string_value(json_object_get(vars,"cmsId")))=="123456");
 assert(std::string(json_string_value(json_object_get(vars,"locale")))=="en_US");
 assert(std::string(json_string_value(json_object_get(root,"query"))).find("addOwnedVariant(language: $locale, variantId: $cmsId)")!=std::string::npos);
 json_decref(root);Reject([]{gfn::ownership::RequestBody("");});Reject([]{gfn::ownership::RequestBody("parent-uuid");});
 const std::string ok=R"({"data":{"addOwnedVariant":{"app":{"id":"app"}}}})";
 gfn::ownership::ValidateResponse(ok,"app");
 Reject([&]{gfn::ownership::ValidateResponse(ok,"other");});
 for(const auto* invalid:{"{}","invalid",R"({"data":{"addOwnedVariant":null}})",R"({"errors":[{"message":"rejected"}],"data":{"addOwnedVariant":{"app":{"id":"app"}}}})"})
  Reject([&]{gfn::ownership::ValidateResponse(invalid,"app");});
 auto remove=gfn::ownership::RequestBody("123456",false);
 auto* request=json_loads(remove.c_str(),0,nullptr);assert(request);
 assert(std::string(json_string_value(json_object_get(request,"operationName")))=="RemoveOwnedVariant");json_decref(request);
 const std::string removed=R"({"data":{"removeOwnedVariant":{"app":{"id":"app"}}}})";
 gfn::ownership::ValidateResponse(removed,"app",false);
 Reject([&]{gfn::ownership::ValidateResponse(removed,"other",false);});
 Reject([&]{gfn::ownership::ValidateResponse(ok,"app",false);});
 Reject([&]{gfn::ownership::ValidateResponse(removed,"app",true);});
 ownership::RemovalConfirmation cancel("owner","111");cancel.Cancel();assert(!cancel.Approve("owner","111"));
 ownership::RemovalConfirmation yes("owner","111");assert(yes.Approve("owner","111"));assert(!yes.Approve("owner","111"));
 ownership::RemovalConfirmation switched("owner","111");assert(!switched.Approve("other","111"));
 ownership::RemovalConfirmation version("owner","111");assert(!version.Approve("owner","222"));
 auto& state=AppState::Instance();state.ClearSession();AuthSession account;account.user.user_id="one";state.SetSession(account);
 GameInfo game;game.id="app";game.title="Title";game.is_in_library=true;
 game.variants={{"111","Steam","MANUAL","","",false},{"222","Epic","NOT_OWNED","","",false}};
 PublicGame store;store.id="app";store.variants={{"111","Steam","NOT_OWNED","","",false},{"222","Epic","NOT_OWNED","","",false}};
 state.SetPublicGames({store});state.MarkGameOwned(game);
 assert(state.library_games().size()==1 && state.public_games()[0].is_in_library);
 assert(state.public_games()[0].variants[0].library_status=="MANUAL" && state.public_games()[0].variants[1].library_status=="NOT_OWNED");
 state.SetLibraryGames({});assert(state.library_games().size()==1); // stale/partial refresh cannot erase an acknowledged addition
 state.SetSession(account);state.SetPublicGames({store});assert(state.public_games()[0].is_in_library);
 state.MarkGameOwned(game);assert(state.library_games().size()==1);
 state.SetLibraryGames({game});state.SetLibraryGames({});assert(state.library_games().empty()); // observed server confirmation retires overlay
 state.MarkGameOwned(game);
 account.user.user_id="two";state.SetSession(account);state.SetLibraryGames({});state.SetPublicGames({store});
 assert(state.library_games().empty() && !state.public_games()[0].is_in_library);
 // Two owned stores: only the selected version is removed, including pending add overlays.
 state.ClearSession();account.user.user_id="one";state.SetSession(account);
 game.variants[1].library_status="MANUAL";state.MarkGameOwned(game);
 state.MarkGameUnowned("app","111");
 assert(state.library_games().size()==1 && state.library_games()[0].variants[1].library_status=="MANUAL");
 assert(state.library_games()[0].variants[0].library_status=="NOT_OWNED");
 state.SetLibraryGames({game});assert(state.library_games()[0].variants[0].library_status=="NOT_OWNED");
 state.MarkGameUnowned("app","222");assert(state.library_games().empty());
 state.SetLibraryGames({game});assert(state.library_games().empty()); // stale list cannot resurrect either version
 store.variants=game.variants;store.is_in_library=true;state.SetPublicGames({store});assert(!state.public_games()[0].is_in_library);
 state.SetLibraryGames({},true); // server confirms removal; subsequent real re-sync is permitted
 state.SetLibraryGames({game},true);assert(state.library_games().size()==1);
 state.MarkGameUnowned("app","111");state.MarkGameOwned(game); // manual re-add cancels the removal overlay
 assert(state.library_games()[0].variants[0].library_status=="MANUAL");
 state.MarkGameUnowned("app","111");account.user.user_id="other";state.SetSession(account);
 state.SetLibraryGames({game});assert(state.library_games()[0].variants[0].library_status=="MANUAL");
 // Adding one version must not restore a different store removed earlier.
 state.MarkGameUnowned("app","222");state.MarkGameOwned(game,"111");
 assert(state.library_games()[0].variants[1].library_status=="NOT_OWNED");
 GameInfo observed=game;observed.variants[1].library_status="NOT_OWNED";
 state.SetLibraryGames({observed},true);assert(state.library_games()[0].variants[1].library_status=="NOT_OWNED");
 GameInfo sparse;sparse.id="app";sparse.is_in_library=true;
 std::vector<GameInfo> sparse_list{sparse};ownership::Remove(sparse_list,{"app","111"});assert(sparse_list.size()==1);
 std::cout<<"Production ownership mutation/response/store-isolation/confirmed-refresh/account-switch tests passed.\n";
}
