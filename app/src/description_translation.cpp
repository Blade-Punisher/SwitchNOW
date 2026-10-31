#include "description_translation.hpp"
#include "atomic_file_replace.hpp"
#include <jansson.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
namespace opennow::description {
namespace {
using Json=std::unique_ptr<json_t,decltype(&json_decref)>;
std::mutex worker;
constexpr size_t kMaxText=32000, kDailyCharacters=5000;
std::string Text(json_t* root,const char* key) { const char* s=root?json_string_value(json_object_get(root,key)):nullptr;return s?s:""; }
std::int64_t Number(json_t* root,const char* key) {return root?json_integer_value(json_object_get(root,key)):0;}
Json Parse(const std::string& value) { json_error_t error{};return Json(json_loadb(value.data(),value.size(),JSON_REJECT_DUPLICATES,&error),json_decref); }
std::string Dump(json_t* root) {char* s=json_dumps(root,JSON_COMPACT);if(!s)return {};std::string out(s);free(s);return out;}
Json Read(const std::string& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in || in.tellg()>8*1024*1024) return Json(nullptr,json_decref);
    in.seekg(0);return Parse(std::string((std::istreambuf_iterator<char>(in)),{}));
}
bool Save(const std::string& path,json_t* root) {
    const auto data=Dump(root);if(data.empty() || data.size()>8*1024*1024)return false;
    const auto temp=path+".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;out.write(data.data(),data.size());out.close();if(!out)return false;}
    return storage::ReplaceWithTemporaryFile(temp,path);
}
void Put(json_t* root,const char* key,const std::string& value){json_object_set_new(root,key,json_string(value.c_str()));}
size_t Characters(const std::string& value){return std::count_if(value.begin(),value.end(),[](unsigned char c){return (c&0xc0)!=0x80;});}
std::string Encode(const std::string& value){
    constexpr char hex[]="0123456789ABCDEF";std::string out;
    for(unsigned char c:value){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}
    return out;
}
std::string Comparable(const std::string& text) {
    std::string out;bool whitespace=false;
    for(unsigned char c:text){if(c==' '||c=='\n'||c=='\t'||c=='\r'){whitespace=true;continue;}if(whitespace&&!out.empty())out+=' ';whitespace=false;out+=char(c);}return out;
}
std::string AppDescription(json_t* data,const char* name,const std::string& id){
    auto* block=data?json_object_get(data,name):nullptr;auto* items=block?json_object_get(block,"items"):nullptr;
    size_t index;json_t* item;json_array_foreach(items,index,item){
        if(Text(item,"id")!=id)continue;
        const auto long_text=Text(item,"longDescription");return PlainText(long_text.empty()?Text(item,"shortDescription"):long_text);
    }return {};
}
}
std::string Locale(const std::string& language){
    if(language=="en")return "en_US";
    if(language=="ru")return "ru_RU";
    if(language=="uk")return "uk_UA";
    if(language=="zh-CN")return "zh_CN";
    if(language=="es")return "es_ES";
    if(language=="it")return "it_IT";
    if(language=="fr")return "fr_FR";
    if(language=="pl")return "pl_PL";
    return {};
}
std::string PlainText(const std::string& text){
    if(text.size()>kMaxText*4)return {};
    std::string out;bool tag=false;
    for(size_t i=0;i<text.size();++i){
        if(text[i]=='<' && text.find('>',i)!=std::string::npos && i+1<text.size() && ((text[i+1]>='a'&&text[i+1]<='z')||(text[i+1]>='A'&&text[i+1]<='Z')||text[i+1]=='/'||text[i+1]=='!')){tag=true;if(text.compare(i,3,"<br")==0||text.compare(i,3,"<p>")==0||text.compare(i,4,"</p>")==0)out+='\n';continue;}
        if(tag && text[i]=='>'){tag=false;continue;}if(tag)continue;
        if(text[i]=='&'){
            const auto end=text.find(';',i);if(end!=std::string::npos && end-i<=10){
                const auto entity=text.substr(i,end-i+1);std::string decoded;
                if(entity=="&amp;")decoded="&";else if(entity=="&lt;")decoded="<";else if(entity=="&gt;")decoded=">";
                else if(entity=="&quot;")decoded="\"";else if(entity=="&#39;"||entity=="&apos;")decoded="'";else if(entity=="&nbsp;")decoded=" ";
                if(!decoded.empty()){out+=decoded;i=end;continue;}
            }
        }
        if(text[i]!='\r')out+=text[i];
    }
    const auto start=out.find_first_not_of(" \t\n");if(start==std::string::npos)return {};
    out=out.substr(start,out.find_last_not_of(" \t\n")-start+1);
    return out.size()<=kMaxText?out:std::string{};
}
std::vector<Segment> Split(const std::string& text){
    std::vector<Segment> result;size_t start=0;
    while(start<text.size()){
        if(text[start]=='\n'||text[start]==' '||text[start]=='\t'){++start;continue;}
        const auto newline=text.find('\n',start);const auto paragraph_end=newline==std::string::npos?text.size():newline;
        auto end=std::min(start+500,paragraph_end);
        if(end<paragraph_end){
            while(end>start && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
            auto boundary=text.find_last_of(".!? ",end-1);
            if(boundary!=std::string::npos && boundary>=start+(end-start)/2 && boundary>start)end=boundary+1;
        }
        if(end<=start)return {};
        auto piece=text.substr(start,end-start);while(!piece.empty()&&(piece.back()==' '||piece.back()=='\t'))piece.pop_back();
        if(!piece.empty())result.push_back({piece,end==paragraph_end?"\n\n":" "});
        start=end;
    }
    if(!result.empty())result.back().separator.clear();
    return result;
}
std::string MetadataBody(const std::string& id,const std::string& language){
    Json root(json_object(),json_decref),vars(json_object(),json_decref);auto* ids=json_array();json_array_append_new(ids,json_string(id.c_str()));
    Put(vars.get(),"vpc","NP-AMS-08");Put(vars.get(),"language",Locale(language));json_object_set_new(vars.get(),"ids",ids);
    Put(root.get(),"query","query SwitchNOWDescription($vpc:String!,$language:String!,$ids:[String]!){english:apps(vpcId:$vpc,language:\"en_US\",appIds:$ids){items{id longDescription shortDescription}} localized:apps(vpcId:$vpc,language:$language,appIds:$ids){items{id longDescription shortDescription}}}");
    json_object_set(root.get(),"variables",vars.get());return Dump(root.get());
}
Result Resolve(const std::string& directory,const std::string& id,const std::string& language,const std::string& original,const Transport& transport,const std::function<bool()>& cancelled,std::int64_t now){
    Result result{PlainText(original),"original",""};
    auto stop=[&]{return cancelled&&cancelled();};
    if(language=="en"||Locale(language).empty()||id.empty())return result;
    std::unique_lock<std::mutex> lock(worker,std::defer_lock);
    while(!lock.try_lock()){if(stop()){result.reason="cancelled";return result;}std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    if(stop()){result.reason="cancelled";return result;}
    const auto cache_path=directory+"/description_cache.json",usage_path=directory+"/translation_usage.json";
    auto cache=Read(cache_path);if(!cache||!json_is_array(cache.get()))cache=Json(json_array(),json_decref);
    json_t* entry=nullptr;size_t index;json_t* candidate;
    json_array_foreach(cache.get(),index,candidate){if(Number(candidate,"schema")==1&&Text(candidate,"id")==id&&Text(candidate,"language")==language&&(Text(candidate,"source")==original||(!result.text.empty()&&Text(candidate,"english")==result.text))){entry=candidate;break;}}
    if(entry && now>=Number(entry,"at") && now-Number(entry,"at")<86400 && !Text(entry,"text").empty())return {Text(entry,"text"),Text(entry,"kind"),"cache"};
    std::string english,localized;
    if(entry&&now>=Number(entry,"at")&&now-Number(entry,"at")<86400){english=Text(entry,"english");localized=Text(entry,"localized");}
    if(english.empty()){
        try{
            const auto response=transport("POST","https://games.geforce.com/graphql",MetadataBody(id,language));
            auto root=Parse(response.body);auto* errors=root?json_object_get(root.get(),"errors"):nullptr;
            if(response.status==200&&root&&!json_array_size(errors)) {
                auto* data=json_object_get(root.get(),"data");english=AppDescription(data,"english",id);localized=AppDescription(data,"localized",id);
            }
        }catch(...){if(stop()){result.reason="cancelled";return result;}}
        if(stop()){result.reason="cancelled";return result;}
        // Existing feed descriptions are explicitly requested as en_US. They
        // remain a usable source when the public metadata endpoint is down.
        if(english.empty())english=entry?Text(entry,"english"):PlainText(original);
        if(english.empty())english=PlainText(original);
        if(english.empty()){result.reason="metadata_missing";return result;}
        const bool same_source=entry && Text(entry,"english")==english;
        if(!entry){entry=json_object();json_array_append_new(cache.get(),entry);}
        else if(!same_source) json_object_clear(entry);
        json_object_set_new(entry,"schema",json_integer(1));Put(entry,"id",id);Put(entry,"language",language);Put(entry,"source",original);Put(entry,"english",english);Put(entry,"localized",localized);
        json_object_set_new(entry,"at",json_integer(now));if(!same_source)json_object_set_new(entry,"pieces",json_array());
    }
    auto persist=[&]{while(json_array_size(cache.get())>64){if(json_array_get(cache.get(),0)==entry){json_incref(entry);json_array_remove(cache.get(),0);json_array_append_new(cache.get(),entry);}else json_array_remove(cache.get(),0);}return Save(cache_path,cache.get());};
    if(!localized.empty()&&Comparable(localized)!=Comparable(english)){Put(entry,"text",localized);Put(entry,"kind","gfn");persist();return {localized,"gfn",""};}
    if(Text(entry,"kind")=="machine"&&!Text(entry,"text").empty()){persist();return {Text(entry,"text"),"machine","cache"};}
    result.text=english;
    const auto segments=Split(english);if(segments.empty()){result.reason="empty";return result;}
    auto* pieces=json_object_get(entry,"pieces");if(!json_is_array(pieces)){pieces=json_array();json_object_set_new(entry,"pieces",pieces);}
    auto usage=Read(usage_path);if(!usage||!json_is_object(usage.get()))usage=Json(json_object(),json_decref);
    const auto day=now/86400;
    if(Number(usage.get(),"day")!=day){const auto blocked=Number(usage.get(),"blocked_until");json_object_clear(usage.get());json_object_set_new(usage.get(),"day",json_integer(day));json_object_set_new(usage.get(),"blocked_until",json_integer(blocked));}
    if(Number(usage.get(),"blocked_until")>now){persist();result.reason="quota";return result;}
    if(Number(usage.get(),"retry_after")>now){persist();result.reason="translation_unavailable";return result;}
    for(size_t i=json_array_size(pieces);i<segments.size();++i){
        if(stop()){persist();result.reason="cancelled";return result;}
        const auto count=Characters(segments[i].text),used=static_cast<size_t>(std::max<std::int64_t>(0,Number(usage.get(),"used")));
        if(count>kDailyCharacters || used>kDailyCharacters-count){persist();result.reason="quota";return result;}
        // Reserve before sending; failed or interrupted network calls can consume quota too.
        json_object_set_new(usage.get(),"used",json_integer(used+count));if(!Save(usage_path,usage.get())){persist();result.reason="storage_failed";return result;}
        Reply response;
        try{response=transport("GET","https://api.mymemory.translated.net/get?q="+Encode(segments[i].text)+"&langpair=en%7C"+Encode(language)+"&mt=1",{});}catch(...){if(!stop()){json_object_set_new(usage.get(),"retry_after",json_integer(now+60));Save(usage_path,usage.get());}persist();result.reason=stop()?"cancelled":"translation_failed";return result;}
        auto root=Parse(response.body);const auto status=Number(root.get(),"responseStatus");
        if(response.status==429||status==429||(root&&json_is_true(json_object_get(root.get(),"quotaFinished")))){
            json_object_set_new(usage.get(),"blocked_until",json_integer(now+86400));Save(usage_path,usage.get());persist();result.reason="quota";return result;
        }
        const auto translated=root?PlainText(Text(json_object_get(root.get(),"responseData"),"translatedText")):std::string{};
        if(response.status!=200||status!=200||translated.empty()){json_object_set_new(usage.get(),"retry_after",json_integer(now+60));Save(usage_path,usage.get());persist();result.reason="translation_failed";return result;}
        json_array_append_new(pieces,json_string(translated.c_str()));persist();
    }
    if(stop()){result.reason="cancelled";return result;}
    std::string translated;
    for(size_t i=0;i<segments.size();++i){const auto* part=json_string_value(json_array_get(pieces,i));if(!part){result.reason="cache_invalid";return result;}translated+=part;translated+=segments[i].separator;if(translated.size()>kMaxText){result.reason="translation_too_large";return result;}}
    Put(entry,"text",translated);Put(entry,"kind","machine");persist();return {translated,"machine",""};
}
}
