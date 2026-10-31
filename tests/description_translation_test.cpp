#include "description_translation.hpp"
#include <jansson.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace opennow::description;
std::string Metadata(const std::string& english,const std::string& localized,const std::string& id="game") {
    auto* root=json_pack("{s:{s:{s:[{s:s,s:s}]},s:{s:[{s:s,s:s}]}}}","data","english","items","id",id.c_str(),"longDescription",english.c_str(),"localized","items","id",id.c_str(),"longDescription",localized.c_str());
    char* s=json_dumps(root,JSON_COMPACT);assert(s);std::string result(s);free(s);json_decref(root);return result;
}
std::string Translation(const std::string& text){auto* root=json_pack("{s:i,s:b,s:{s:s}}","responseStatus",200,"quotaFinished",0,"responseData","translatedText",text.c_str());char* s=json_dumps(root,0);std::string result(s);free(s);json_decref(root);return result;}
int main(){
    const std::string base="build/host-tests/description-fixture";
    std::filesystem::remove_all(base);std::filesystem::create_directories(base);
    auto dir=[&](const std::string& name){const auto value=base+"/"+name;std::filesystem::create_directories(value);return value;};
    const auto never=[] {return false;};const std::int64_t now=2000000000;
    for(const auto* language:{"en","ru","uk","zh-CN","es","it","fr","pl"}) assert(!Locale(language).empty());
    assert(Locale("invalid").empty());assert(Locale("uk")=="uk_UA");
    assert(PlainText("<p>Hello &amp; welcome</p><br>Game")=="Hello & welcome\n\nGame");
    auto chunks=Split(std::string(1100,'x')+"\nSecond paragraph.");assert(chunks.size()==4);
    for(const auto& part:chunks)assert(!part.text.empty()&&part.text.size()<=500);
    const std::string unicode=[] {std::string s;for(int i=0;i<350;i++)s+="界";return s;}();
    for(const auto& part:Split(unicode)){json_t* str=json_string(part.text.c_str());assert(str);json_decref(str);assert(part.text.size()<=500);}
    int metadata=0,translations=0;
    auto official=[&](const std::string& method,const std::string&,const std::string& body){assert(method=="POST");assert(body.find("ru_RU")!=std::string::npos);++metadata;return Reply{200,Metadata("English text","Русское описание")};};
    const auto official_dir=dir("official");auto r=Resolve(official_dir,"game","ru","English text",official,never,now);
    assert(r.kind=="gfn"&&r.text=="Русское описание"&&metadata==1);
    r=Resolve(official_dir,"game","ru","English text",official,never,now+1);assert(r.reason=="cache"&&metadata==1);
    r=Resolve(official_dir,"game","ru","Changed source",official,never,now+2);assert(metadata==2);
    auto machine=[&](const std::string& method,const std::string& url,const std::string&){if(method=="POST"){++metadata;return Reply{200,Metadata("English text","English text")};}++translations;assert(url.find("langpair=en%7Cru")!=std::string::npos);return Reply{200,Translation("Перевод текста")};};
    const auto machine_dir=dir("machine");r=Resolve(machine_dir,"game","ru","English text",machine,never,now);assert(r.kind=="machine"&&r.text=="Перевод текста"&&translations==1);
    r=Resolve(machine_dir,"game","ru","English text",machine,never,now+1);assert(r.reason=="cache"&&translations==1);
    const auto quota_dir=dir("quota");int calls=0;
    auto quota=[&](const std::string& method,const std::string&,const std::string&){if(method=="POST")return Reply{200,Metadata("English text","English text")};++calls;return Reply{429,"{}"};};
    r=Resolve(quota_dir,"game","uk","English text",quota,never,now);assert(r.kind=="original"&&r.reason=="quota"&&calls==1);
    r=Resolve(quota_dir,"game","uk","English text",quota,never,now+60);assert(r.reason=="quota"&&calls==1);
    // A UTC date rollover must not bypass the provider's 24-hour cooldown.
    r=Resolve(quota_dir,"game","uk","English text",quota,never,now+10000);assert(r.reason=="quota"&&calls==1);
    const auto partial_dir=dir("partial");bool fail=true;int chunks_sent=0;const auto english=std::string(600,'x');
    auto partial=[&](const std::string& method,const std::string&,const std::string&){if(method=="POST")return Reply{200,Metadata(english,english)};++chunks_sent;if(fail&&chunks_sent==2)return Reply{503,"{}"};return Reply{200,Translation("часть")};};
    r=Resolve(partial_dir,"game","ru",english,partial,never,now);assert(r.kind=="original"&&chunks_sent==2);
    fail=false;r=Resolve(partial_dir,"game","ru",english,partial,never,now+120);assert(r.kind=="machine"&&chunks_sent==3); // First piece reused, not billed again.
    const auto error_dir=dir("errors");r=Resolve(error_dir,"game","ru","Original",[](const auto&,const auto&,const auto&){return Reply{200,"{\"errors\":[{}]}"};},never,now);assert(r.text=="Original"&&r.kind=="original");
    r=Resolve(error_dir,"game","ru","Original",official,[]{return true;},now);assert(r.reason=="cancelled");
    r=Resolve(error_dir,"game","en","Original",official,never,now);assert(r.text=="Original");
    const auto wrong=dir("wrong");r=Resolve(wrong,"game","ru","Original",[](const auto&,const auto&,const auto&){return Reply{200,Metadata("English","Русский","other-id")};},never,now);assert(r.kind=="original"&&r.text=="Original");
    // Language-specific cache must not return a Russian entry for Ukrainian.
    r=Resolve(machine_dir,"game","uk","English text",quota,never,now);assert(r.kind=="original");

    // Persist partial work across the daily limit and the next metadata refresh.
    const auto day_dir=dir("next-day");int day_calls=0;const std::string large(5100,'x');
    auto daily=[&](const std::string& method,const std::string&,const std::string&){if(method=="POST")return Reply{200,Metadata(large,large)};++day_calls;return Reply{200,Translation("готово")};};
    r=Resolve(day_dir,"game","ru",large,daily,never,now);assert(r.kind=="original"&&r.reason=="quota"&&day_calls==10);
    r=Resolve(day_dir,"game","ru",large,daily,never,now+86401);assert(r.kind=="machine"&&day_calls==11);
    // Finished machine translations survive a new day without using more quota.
    r=Resolve(day_dir,"game","ru",large,daily,never,now+172802);assert(r.reason=="cache"&&day_calls==11);
    const auto whitespace_dir=dir("whitespace");int whitespace_calls=0;
    r=Resolve(whitespace_dir,"game","ru","English text",[&](const std::string& method,const auto&,const auto&){if(method=="POST")return Reply{200,Metadata("English text","English  text")};++whitespace_calls;return Reply{200,Translation("Текст")};},never,now);
    assert(r.kind=="machine"&&whitespace_calls==1);
    int no_storage_calls=0;
    r=Resolve(base+"/missing/directory","game","ru","English text",[&](const std::string& method,const auto&,const auto&){if(method=="POST")return Reply{200,Metadata("English text","English text")};++no_storage_calls;return Reply{200,Translation("Текст")};},never,now);
    assert(r.reason=="storage_failed"&&no_storage_calls==0);
    std::cout<<"Production description locale/exact-ID/HTML/UTF8/chunk/cache/invalidation/partial-resume/quota/cancel tests passed.\n";
}
