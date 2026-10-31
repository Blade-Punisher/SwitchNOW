#include "home_record_policy.hpp"
#include <vector>
#include <cassert>
using namespace opennow::shortcut;
int main() {
    std::vector<uint8_t> records(48,0);
    records[0]=42;records[12]=0x80;records[16]=5;
    // Preserve unrelated/add-on records byte-for-byte.
    for(size_t i=24;i<48;++i)records[i]=static_cast<uint8_t>(i);
    const auto original=records;
    ValidateHomeRecords(records,42);assert(records==original);
    auto rejects=[](const auto& bytes,uint64_t title){
        bool threw=false;try{ValidateHomeRecords(bytes,title);}catch(const std::runtime_error&){threw=true;}
        assert(threw);
    };
    rejects(records,43);
    records[16]=4;rejects(records,42);
    records=original;records[13]=1;rejects(records,42);
    records=original;records[12]=0x81;rejects(records,42);
    records=original;records.pop_back();rejects(records,42);
    rejects(std::vector<uint8_t>{},42);
    records=original;records.resize(257*24);rejects(records,42);
}
