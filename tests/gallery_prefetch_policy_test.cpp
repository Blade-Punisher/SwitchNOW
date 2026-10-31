#include "gallery_prefetch_policy.hpp"
#include <cassert>
int main(){
    assert(opennow::GalleryPrefetchWindow(0,9,1).count==0);
    for(size_t total=1;total<50;++total)for(size_t selected=0;selected<total;++selected)for(int direction:{-1,1}){
        const auto window=opennow::GalleryPrefetchWindow(total,selected,direction);
        assert(window.count==std::min<size_t>(total,4)&&window.indices[0]==selected);
        for(size_t i=0;i<window.count;++i){assert(window.indices[i]<total);for(size_t j=0;j<i;++j)assert(window.indices[i]!=window.indices[j]);}
        if(total>1)assert(window.indices[1]==(direction>0?(selected+1)%total:(selected+total-1)%total));
    }
    assert((opennow::GalleryPrefetchWindow(8,0,1).indices==std::array<size_t,4>{0,1,2,7}));
    assert((opennow::GalleryPrefetchWindow(8,0,-1).indices==std::array<size_t,4>{0,7,6,1}));
}
