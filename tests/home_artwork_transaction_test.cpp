#include "home_artwork_transaction.hpp"
#include <cassert>
#include <string>
using namespace opennow::shortcut;
void Scenario(int fail) {
    std::vector<uint8_t> previous{1,2},next{3,4},working=previous,disk=previous;
    int commits=0,writes=0,invalidations=0;bool restored=false,succeeded=false;
    auto error=[] {throw std::runtime_error("injected failure");};
    try {CommitHomeArtwork(previous,next,{
        [&]{if(fail==1 && writes==0)return next;return disk;},
        [&](const auto& bytes){++writes;working=bytes;if(fail==2&&writes==1)error();},
        [&]{++commits;if(fail==3&&commits==1)error();disk=working;},
        [&]{++invalidations;if(fail==4&&invalidations==1)error();},
        [&]{return fail!=5;},
        [&](bool result){restored=result;}
    });succeeded=true;}catch(const std::exception&){}
    if(fail==0){assert(succeeded&&disk==next&&writes==1&&commits==1);}
    else if(fail==1){assert(!succeeded&&disk==previous&&writes==0);}
    else {assert(!succeeded&&disk==previous&&restored&&writes==2);}
}
int main(){
    for(int fail=0;fail<=5;++fail)Scenario(fail);
    for(int fail=0;fail<=2;++fail) {
        std::string order;
        bool succeeded=false;
        try {
            succeeded=VerifyHomeArtworkAndNotify(
                [&]{order+='S';return fail!=1;},
                [&]{order+='C';return fail!=2;},
                [&]{order+='N';});
        }catch(const std::runtime_error&){}
        assert(succeeded==(fail==0));
        assert(order==(fail==0?"SCN":fail==1?"S":"SC"));
    }
}
