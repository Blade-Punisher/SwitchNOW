#include "localization.hpp"
#include <algorithm>
#include <array>
#include <string_view>
namespace opennow {
namespace {
using namespace std::literals;
struct TranslationEntry { std::string_view key; std::array<std::string_view,7> text; };
#include "localization_catalog.inc"
constexpr std::array<std::string_view,7> kCodes{"ru","uk","es","it","fr","pl","zh-CN"};
std::string g_language="en";
size_t g_language_index=kCodes.size();
size_t LanguageIndex(std::string_view code) {
    const auto found=std::find(kCodes.begin(),kCodes.end(),code);
    return static_cast<size_t>(found-kCodes.begin());
}
const TranslationEntry* Find(std::string_view key) {
    const auto found=std::lower_bound(std::begin(kCatalog),std::end(kCatalog),key,
        [](const auto& entry,std::string_view value){return entry.key<value;});
    return found!=std::end(kCatalog)&&found->key==key?found:nullptr;
}
bool MatchPattern(std::string_view message,std::string_view pattern,std::vector<std::string>& values) {
    size_t at=0,pos=0;
    while(pos<pattern.size()) {
        const auto open=pattern.find('{',pos);
        const auto literal=pattern.substr(pos,open==std::string_view::npos?pattern.size()-pos:open-pos);
        if(message.substr(at,literal.size())!=literal)return false;
        at+=literal.size();if(open==std::string_view::npos)return at==message.size();
        const auto close=pattern.find('}',open);if(close==std::string_view::npos)return false;
        const auto index=pattern[open+1]-'0';if(index<0||index>9)return false;
        pos=close+1;const auto next=pattern.find('{',pos);
        const auto separator=pattern.substr(pos,next==std::string_view::npos?pattern.size()-pos:next-pos);
        const auto end=separator.empty()?message.size():message.find(separator,at);
        if(end==std::string_view::npos||end==at)return false;
        if(values.size()<=static_cast<size_t>(index))values.resize(index+1);
        values[index]=message.substr(at,end-at);at=end;
    }
    return at==message.size();
}
}
const std::vector<InterfaceLanguageOption>& InterfaceLanguageOptions() {
    static const std::vector<InterfaceLanguageOption> options={
        {"en","English"},{"zh-CN","简体中文"},{"es","Español"},{"ru","Русский"},
        {"it","Italiano"},{"fr","Français"},{"pl","Polski"},{"uk","Українська"}};
    return options;
}
std::string InterfaceLanguageLabel(const std::string& code) {
    const auto& options=InterfaceLanguageOptions();
    const auto found=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.code==code;});
    return found==options.end()?"English":found->label;
}
bool IsSupportedInterfaceLanguage(const std::string& code){return code=="en"||LanguageIndex(code)<kCodes.size();}
void SetInterfaceLanguage(const std::string& code){g_language=IsSupportedInterfaceLanguage(code)?code:"en";g_language_index=LanguageIndex(g_language);}
const std::string& GetInterfaceLanguage(){return g_language;}
std::string Tr(const std::string& key) {
    if(g_language_index==kCodes.size()||key.empty())return key;
    const auto* found=Find(key);return found?std::string(found->text[g_language_index]):key;
}
std::string Tr(const char* key){return Tr(std::string(key?key:""));}
bool HasTranslation(const std::string& key,const std::string& language) {
    if(language=="en")return true;
    const auto* found=Find(key);const auto index=LanguageIndex(language);
    return found&&index<kCodes.size()&&!found->text[index].empty();
}
std::vector<std::string> TranslationKeys(){std::vector<std::string> result;for(const auto& entry:kCatalog)result.emplace_back(entry.key);return result;}
std::string TranslationSource(const std::string& text,const std::string& language) {
    if(Find(text))return text;
    const auto index=LanguageIndex(language);if(index<kCodes.size())for(const auto& entry:kCatalog)if(entry.text[index]==text)return std::string(entry.key);
    return text;
}
std::string TrFormat(const std::string& key,const std::vector<std::string>& values) {
    const auto pattern=Tr(key);std::string result;
    for(size_t pos=0;pos<pattern.size();) {
        if(pattern[pos]=='{') {
            const auto close=pattern.find('}',pos+1);size_t index=0,n=pos+1;
            for(;close!=std::string::npos&&n<close&&pattern[n]>='0'&&pattern[n]<='9'&&index<1000;++n)index=index*10+pattern[n]-'0';
            if(close!=std::string::npos&&n==close&&n>pos+1&&index<values.size()){result+=values[index];pos=close+1;continue;}
        }
        result+=pattern[pos++];
    }
    return result;
}
std::string TrMessage(const std::string& message) {
    if(g_language_index==kCodes.size()||Find(message))return Tr(message);
    static constexpr std::array patterns{
        "{0}h {1}m left"sv,"{0}m left"sv,"{0}h left"sv,
        "NVIDIA sent an approval email. Open it and approve this sign-in. Waiting: {0} seconds (timeout: 180 seconds). Check Spam if needed."sv,
        "{0} GB used of {1} GB"sv,"{0} GB total"sv,"{0} GB left"sv,"Session 0:{0}"sv};
    for(const auto pattern:patterns){std::vector<std::string> values;if(MatchPattern(message,pattern,values))return TrFormat(std::string(pattern),values);}
    return message;
}
}
