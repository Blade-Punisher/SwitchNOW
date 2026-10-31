#include "gfn/library_catalog.hpp"
#include <jansson.h>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace opennow::gfn::library;
static std::string Page(unsigned start, unsigned count, bool next, const char* cursor) {
    json_t* root=json_object(); json_t* data=json_object(); json_t* apps=json_object();
    json_t* items=json_array(); json_t* info=json_pack("{s:b,s:s}","hasNextPage",next,"endCursor",cursor);
    for(unsigned i=start;i<start+count;++i) {
        auto id=std::to_string(i);
        json_t* app=json_pack("{s:s,s:s,s:[{s:{s:{s:s,s:b}}}]}",
            "id",id.c_str(),"title",id.c_str(),"variants","gfn","library","status",
            i%4==0?"NOT_IN_LIBRARY":i%4==1?"MANUAL":i%4==2?"PLATFORM_SYNC":"IN_LIBRARY","selected",false);
        assert(app); json_array_append_new(items,app);
    }
    json_object_set_new(apps,"items",items);json_object_set_new(apps,"pageInfo",info);
    json_object_set_new(data,"apps",apps);json_object_set_new(root,"data",data);
    char* raw=json_dumps(root,JSON_COMPACT);assert(raw);
    std::string result(raw);free(raw);json_decref(root);return result;
}
template<class F> static void Reject(F fetch) {
    bool failed=false; try { (void)Scan(fetch); } catch(const std::runtime_error&) { failed=true; }
    assert(failed);
}
int main() {
    unsigned calls=0, previews=0;
    auto result=Scan([&](const std::string& cursor) {
        ++calls;
        if(calls==1) {assert(cursor.empty());return Page(0,200,true,"page2");}
        assert(cursor=="page2");return Page(200,120,false,"");
    }, [&](const ScanResult& partial) {
        ++previews; assert(partial.pages==calls && previews==calls);
        assert(partial.owned_apps.size()==(calls==1?150:240));
    });
    assert(previews==2);
    assert(calls==2 && result.pages==2 && result.scanned==320 && result.owned_apps.size()==240);
    result=Scan([](const auto&){return Page(0,0,false,"");});
    assert(result.pages==1 && result.owned_apps.empty()); // valid empty account
    calls=0;
    result=Scan([&](const auto&) {return ++calls==1?Page(0,200,true,"p2"):Page(0,200,false,"");});
    assert(result.owned_apps.size()==150); // repeated games do not inflate totals
    Reject([](const auto&){return std::string("{}");});
    Reject([](const auto&){return std::string("not json");});
    Reject([](const auto&){return std::string("{\"errors\":[{\"message\":\"schema\"}]}");});
    Reject([](const auto&){return Page(0,3,true,"");});
    Reject([](const auto&){return Page(0,3,true,"repeat");});
    Reject([](const auto&) {
        auto page=Page(0,3,false,"");
        json_t* root=json_loads(page.c_str(),0,nullptr);assert(root);
        auto info=json_object_get(json_object_get(json_object_get(root,"data"),"apps"),"pageInfo");
        json_object_set_new(info,"totalCount",json_integer(40));
        char* raw=json_dumps(root,JSON_COMPACT);assert(raw);
        std::string result(raw);free(raw);json_decref(root);return result;
    });
    calls=0;
    Reject([&](const auto&) {return Page(0,0,true,std::to_string(++calls).c_str());});
    assert(calls==96);
    calls=0;
    Reject([&](const auto&) -> std::string {if(++calls==1)return Page(0,200,true,"p2");throw std::runtime_error("HTTP503");});
    // Progress remains provisional when a later page fails.
    calls=0;previews=0;
    bool interrupted=false;
    try {
        (void)Scan([&](const auto&) -> std::string {
            if(++calls==1) return Page(0,20,true,"next");
            throw std::runtime_error("offline");
        }, [&](const ScanResult& partial) {++previews;assert(partial.owned_apps.size()==15);});
    } catch (const std::runtime_error&) {interrupted=true;}
    assert(interrupted && previews==1);
    auto body=RequestBody("vpc\"\\", "cursor\"\\");
    json_t* parsed=json_loads(body.c_str(),0,nullptr);assert(parsed);
    auto vars=json_object_get(parsed,"variables");
    assert(std::string(json_string_value(json_object_get(vars,"vpcId")))=="vpc\"\\");
    assert(json_integer_value(json_object_get(vars,"fetchCount"))==200);
    assert(json_is_object(json_object_get(vars,"filters")));
    json_decref(parsed);
    assert(!OwnedStatus("UNKNOWN") && !OwnedStatus("NOT_IN_LIBRARY"));
    std::cout << "Complete-library pagination/ownership/error tests passed: 240 owned games across two pages.\n";
}
