#include "../extern/borealis/library/include/borealis/extern/nanovg/framework/CDeferredGpuRelease.h"
#include <cassert>
#include <memory>
namespace { bool busy=false; unsigned destroyed=0;
struct Image {~Image(){assert(!busy);++destroyed;}}; }
int main() {
    CDeferredGpuRelease<std::shared_ptr<Image>> queue;
    auto image=std::make_shared<Image>();std::weak_ptr<Image> weak=image;
    busy=true;queue.Retire(image);image.reset();
    assert(!weak.expired() && destroyed==0);
    busy=false;queue.ReleaseAfterFence();
    assert(weak.expired() && destroyed==1);
    for(unsigned frame=0;frame<1000;++frame) {
        busy=true;queue.Retire(std::make_shared<Image>());
        assert(destroyed==frame+1);
        busy=false;queue.ReleaseAfterFence();
    }
    assert(destroyed==1001);
}
