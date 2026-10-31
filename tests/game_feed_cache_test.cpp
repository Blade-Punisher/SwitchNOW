#include "gfn/game_feed_cache.hpp"
#include "game_feed_models.hpp"
#include "game_feed_policy.hpp"
#include "http_cancellation.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace opennow;
int main() {
 const auto dir=fs::temp_directory_path()/"switchnow-feed-cache-test";
 fs::create_directories(dir);
 const auto store=game_feed::ScopeKey("store","test-account");
 const auto library=game_feed::ScopeKey("library","test-account");
 PublicGame game; game.id="id"; game.uuid="uuid"; game.title="Game"; game.store="Steam";
 game.description="Description"; game.screenshots={"shot1","shot2"}; game.hero_url="hero";
 game.variants.push_back({"variant","Steam","MANUAL","date","AVAILABLE",true});game.is_in_library=true;
 const auto record=game_feed::ToRecord(game); const auto converted=game_feed::ToPublic(record);
 assert(converted.store==game.store && converted.variants[0].library_selected && converted.screenshots==game.screenshots);
 gfn::feed::CachedFeed snapshot{{record},100,110};
 assert(gfn::feed::SaveCache(dir.string(),store,snapshot));
 // Cancellation at any cache-write checkpoint must preserve the prior good file.
 for(unsigned checkpoint=1;checkpoint<=3;++checkpoint) {
  unsigned checks=0;
  HttpCancellationScope cancel([&]{return ++checks>=checkpoint;});
  auto stale=snapshot;stale.games[0].description="cancelled result";
  assert(!gfn::feed::SaveCache(dir.string(),store,stale));
  assert(gfn::feed::LoadCache(dir.string(),store)->games[0].description=="Description");
  assert(!fs::exists(gfn::feed::CachePath(dir.string(),store)+".tmp"));
 }
 const auto loaded=gfn::feed::LoadCache(dir.string(),store);assert(loaded && loaded->games.size()==1);
 assert(loaded->games[0].description==game.description && loaded->scanned_at_ms==100 && loaded->updated_at_ms==110);
 assert(!gfn::feed::LoadCache(dir.string(),library));
 assert(!gfn::feed::LoadCache(dir.string(),"other-account"));
 auto invalid=snapshot; invalid.games[0].id.clear(); assert(!gfn::feed::SaveCache(dir.string(),store,invalid));
 invalid=snapshot;invalid.games[0].screenshots.resize(129);assert(!gfn::feed::SaveCache(dir.string(),store,invalid));
 assert(gfn::feed::LoadCache(dir.string(),store)->games[0].id=="id");
 const auto path=gfn::feed::CachePath(dir.string(),store);
 fs::copy_file(path,path+".bak",fs::copy_options::overwrite_existing);
 {std::ofstream output(path);output<<"truncated";}
 assert(gfn::feed::LoadCache(dir.string(),store));
 fs::remove(path+".bak"); assert(!gfn::feed::LoadCache(dir.string(),store));
 assert(gfn::feed::SaveCache(dir.string(),library,{{},100,100}));
 assert(gfn::feed::LoadCache(dir.string(),library)->games.empty());
 // A complete cache cannot be loaded under another feed/account even if copied.
 fs::copy_file(gfn::feed::CachePath(dir.string(),library),path,fs::copy_options::overwrite_existing);
 assert(!gfn::feed::LoadCache(dir.string(),store));
 fs::remove(path);fs::remove(gfn::feed::CachePath(dir.string(),library));
 const auto old_revision=game_feed::Revision();
 assert(gfn::feed::SaveCache(dir.string(),store,snapshot,old_revision));
 assert(gfn::feed::SaveCache(dir.string(),library,snapshot,old_revision));
 gfn::feed::InvalidateAccount(dir.string(),"test-account");
 assert(game_feed::Revision()!=old_revision);
 assert(!gfn::feed::LoadCache(dir.string(),store) && !gfn::feed::LoadCache(dir.string(),library));
 assert(!gfn::feed::SaveCache(dir.string(),library,snapshot,old_revision));
 fs::remove(dir);
 std::cout<<"Production shared feed cache roundtrip/isolation/corruption/backup/empty and cancellation-before-commit tests passed.\n";
}
