#pragma once
#include <array>
#include <cstddef>
#include <algorithm>
namespace opennow {
struct GalleryWindow { std::array<size_t,4> indices{};size_t count=0; };
inline GalleryWindow GalleryPrefetchWindow(size_t total,size_t selected,int direction) {
    GalleryWindow result;if(!total)return result;selected=std::min(selected,total-1);
    const auto step=[total](size_t at,bool forward){return forward?(at+1==total?0:at+1):(at==0?total-1:at-1);};
    const bool forward=direction>=0;
    for(const auto index:{selected,step(selected,forward),step(step(selected,forward),forward),step(selected,!forward)}) {
        if(std::find(result.indices.begin(),result.indices.begin()+result.count,index)==result.indices.begin()+result.count)result.indices[result.count++]=index;
    }
    return result;
}
}
