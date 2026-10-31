#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
namespace opennow::shortcut {
inline void ValidateHomeRecords(std::span<const uint8_t> records,uint64_t title) {
    if(records.empty() || records.size()%24 || records.size()>256*24)
        throw std::runtime_error("Invalid HOME record list");
    bool found=false;
    for(size_t at=0;at<records.size();at+=24) {
        uint64_t id=0;for(size_t i=0;i<8;++i)id|=uint64_t(records[at+i])<<(8*i);
        // NCM Application (0x80), Full install (0), SD storage (5).
        if(id==title && records[at+12]==0x80 && records[at+13]==0 && records[at+16]==5)
            found=true;
    }
    if(!found)throw std::runtime_error("HOME record has no matching installed SD application");
}
}
