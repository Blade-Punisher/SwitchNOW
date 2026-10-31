#include "game_feed_policy.hpp"
#include "http_cancellation.hpp"
#include "game_feed_page_policy.hpp"
#include <cassert>
#include <iostream>
using namespace opennow;
int main() {
 using namespace game_feed;
 assert(!Fresh(0, 10, 100)); assert(!Fresh(101, 100, 100));
 assert(Fresh(100, 199, 100)); assert(!Fresh(100,200,100));
 assert(ScanRequired(true,true,100,101)); assert(ScanRequired(false,false,100,101));
 assert(!ScanRequired(false,true,100,101));
 assert(OwnerKey("ab","c","d") != OwnerKey("a","bc","d"));
 assert(ScopeKey("store","account") != ScopeKey("library","account"));
 assert(ScopeKey("library","").empty());
 for (std::size_t total=0; total<500; ++total) {
    std::size_t covered=0;
    for (std::size_t requested=0; requested<Window(total,0).count; ++requested) {
        const auto page=Window(total,requested);
        assert(page.start==covered && page.end>=page.start && page.end-page.start<=15);
        covered=page.end; assert(page.Remaining()==total-covered);
        assert(page.HasNext()==(requested+1<page.count));
    }
    assert(covered==total);
    assert(Window(total,999).index==Window(total,0).count-1);
 }
 for (std::size_t count=1; count<400; ++count) {
    for(std::size_t index=0; index<count; ++index) {
        const auto page=Window(count*kPageSize,index);
        const auto values=PageNumbers(page);
        assert(values.size()<=11 && values.front()==1 && values.back()==count);
        assert(std::find(values.begin(),values.end(),index+1)!=values.end());
        if(page.HasNext()) assert(std::find(values.begin(),values.end(),index+2)!=values.end());
        std::size_t previous=0;
        for(auto value:values) if(value!=0) {assert(value>previous && value<=count);previous=value;}
    }
 }
 assert(PageNumbers(Window(0,0)).empty());
 assert(!ParsePage(0,10) && !ParsePage(-5,10) && !ParsePage(11,10));
 assert(ParsePage(1,10)==0 && ParsePage(10,10)==9);
 assert(DescribeEmpty(true,false,false)==EmptyState::Loading);
 assert(DescribeEmpty(false,true,true)==EmptyState::NoMatches);
 assert(DescribeEmpty(false,false,false)==EmptyState::Incomplete);
 assert(DescribeEmpty(false,false,true)==EmptyState::Empty);
 Requests requests; auto old = requests.Begin(); assert(requests.Current(old));
 auto next=requests.Begin(); assert(!requests.Current(old)); assert(requests.Current(next));
 requests.Invalidate(); assert(!requests.Current(next));
 LaunchGate gate; assert(!gate.Check(false)); assert(gate.Check(true)); assert(gate.Check(false));
 assert(!ShowFailure(true,false,true,true)); assert(!ShowFailure(false,true,true,true));
 assert(!ShowFailure(false,false,false,true)); assert(!ShowFailure(false,false,true,false));
 assert(ShowFailure(false,false,true,true));
 bool launching=false;
 { HttpCancellationScope scope([&]{return gate.Check(launching);});
   auto callback=HttpCancellationScope::Callback(); assert(callback()); }
 assert(!HttpCancellationScope::Callback());
 std::cout<<"Shared game-feed freshness/account/scope/cancellation/generation tests passed. Shared pagination bounds/coverage tests passed.\n";
}
