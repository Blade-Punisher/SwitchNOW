#include "library_metadata_policy.hpp"
#include <cassert>
#include <iostream>
using namespace opennow;
using namespace opennow::game_feed;
int main() {
    GameInfo kept;kept.id="kept";kept.description="saved description";kept.screenshots={"saved screen"};kept.publisher="publisher";
    kept.is_in_library=true;kept.available_stores={"old store"};kept.image_url="old art";
    kept.variants.push_back({"old variant","EPIC","MANUAL","","",true});
    GameInfo removed=kept;removed.id="removed";
    GameInfo server;server.id="kept";server.image_url="new art";server.available_stores={"STEAM"};
    GameInfo added;added.id="new";added.description="new description";
    std::vector<GameInfo> result{server,added};ReuseLibraryMetadata(result,{kept,removed});
    assert(result.size()==2&&result[0].id=="kept"&&result[1].id=="new");
    assert(result[0].description==kept.description&&result[0].screenshots==kept.screenshots&&result[0].publisher==kept.publisher);
    assert(result[0].variants.empty()&&!result[0].is_in_library&&result[0].available_stores==server.available_stores);
    assert(result[0].image_url=="new art"&&!NeedsLibraryMetadata(result[0]));
    assert(NeedsLibraryMetadata(result[1])&&result[1].description=="new description");
    result[0].description="fresh description";result[0].screenshots={"fresh screen"};ReuseLibraryMetadata(result,{kept});
    assert(result[0].description=="fresh description"&&result[0].screenshots[0]=="fresh screen");
    std::cout<<"Library metadata reuse preserves current artwork/ownership/variants, excludes removed titles, respects server descriptions/screenshots and selects missing details.\n";
}
