#include "app_state.hpp"
#include <cassert>
#include <iostream>
using namespace opennow;
int main() {
 auto& state=AppState::Instance();state.ClearSession();
 AuthSession account;account.user.user_id="a";account.provider.code="gfn";account.provider.streaming_service_url="service";
 state.SetSession(account);state.SetLibraryGames({GameInfo{}});state.SetPublicGames({PublicGame{}});
 state.SetSession(account);assert(state.HasLibraryGames() && state.HasPublicGames());
 account.provider.code="other";state.SetSession(account);assert(!state.HasLibraryGames() && !state.HasPublicGames());
 state.SetLibraryGames({GameInfo{}});state.SetPublicGames({PublicGame{}});state.ClearSession();
 assert(!state.HasLibraryGames() && !state.HasPublicGames());
 std::cout<<"Production account feed invalidation/token-refresh retention tests passed.\n";
}
