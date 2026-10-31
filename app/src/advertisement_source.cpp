#include "advertisement_source.hpp"
#include "media_input_policy.hpp"
#include <borealis/extern/tinyxml2/tinyxml2.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace opennow {
namespace {
std::string Trim(std::string s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
std::string Resolve(const std::string& base,const std::string& reference) {
    std::string result=Trim(reference);
    if(result.find_first_of("\r\n")!=std::string::npos)
        throw std::runtime_error("Invalid advertisement resource URL");
    if(result.rfind("http://",0)==0 || result.rfind("https://",0)==0)return result;
    const auto scheme=base.find("://");
    if(scheme==std::string::npos)throw std::runtime_error("Invalid advertisement resource URL");
    if(result.rfind("//",0)==0)return base.substr(0,scheme+1)+result;
    if(media_input::HasScheme(result))throw std::runtime_error("Unsupported advertisement resource scheme");
    const auto host_end=base.find_first_of("/?#",scheme+3);
    const std::string origin=base.substr(0,host_end);
    if(result.empty())return base;
    if(result.front()=='?')return base.substr(0,base.find_first_of("?#"))+result;
    if(result.front()=='#')return base.substr(0,base.find('#'))+result;
    std::string path;
    if(result.front()=='/')path=result;
    else {
        std::string base_path=host_end==std::string::npos ? "/" : base.substr(host_end);
        base_path=base_path.substr(0,base_path.find_first_of("?#"));
        if(base_path.empty() || base_path.front()!='/')base_path="/";
        path=base_path.substr(0,base_path.rfind('/')+1)+result;
    }
    const auto suffix_start=path.find_first_of("?#");
    const std::string suffix=suffix_start==std::string::npos ? "" : path.substr(suffix_start);
    path=path.substr(0,suffix_start);
    std::vector<std::string> parts;
    std::istringstream segments(path);std::string segment;
    while(std::getline(segments,segment,'/')) {
        if(segment=="..") {if(!parts.empty())parts.pop_back();}
        else if(segment!="." && !segment.empty())parts.push_back(segment);
    }
    result=origin;
    for(const auto& part:parts)result+="/"+part;
    if(parts.empty() || path.back()=='/')result+="/";
    result+=suffix;
    if(result.rfind("http://",0)!=0 && result.rfind("https://",0)!=0)
        throw std::runtime_error("Unsupported advertisement resource scheme");
    return result;
}
std::string Text(const tinyxml2::XMLElement* element) {
    return element && element->GetText() ? Trim(element->GetText()) : std::string();
}
void FindVast(const tinyxml2::XMLElement* node, std::string& media,std::string& wrapper,int& score,int depth=0) {
    if(!node || depth>32)return;
    const std::string name=node->Name();
    if(name=="MediaFile") {
        const std::string type=node->Attribute("type") ? node->Attribute("type") : "";
        int candidate=type=="video/mp4" ? 300 : (type=="video/webm" ? 100 : 0);
        const std::string delivery=node->Attribute("delivery") ? node->Attribute("delivery") : "";
        if(delivery=="streaming")candidate=0;
        const int height=node->IntAttribute("height",0);
        if(height>720)candidate-=50;
        const auto text=Text(node);
        if(candidate>score && !text.empty()){score=candidate;media=text;}
    }
    if(name=="VASTAdTagURI" && wrapper.empty())wrapper=Text(node);
    for(auto child=node->FirstChildElement();child;child=child->NextSiblingElement())
        FindVast(child,media,wrapper,score,depth+1);
}
}
AdvertisementSource::AdvertisementSource(std::string prefix,Download download,
    std::function<bool()> cancelled,Report report):prefix_(std::move(prefix)),
    download_(std::move(download)),cancelled_(std::move(cancelled)),report_(std::move(report)){}
AdvertisementSource::~AdvertisementSource(){for(const auto& path:files_)std::remove(path.c_str());}
std::string AdvertisementSource::Prepare(const std::string& url){return Fetch(Resolve(url,url),0);}
std::string AdvertisementSource::Fetch(const std::string& url,int depth) {
    if(cancelled_ && cancelled_())throw std::runtime_error("Advertisement download cancelled");
    const auto cached=ready_.find(url);
    if(cached!=ready_.end())return cached->second;
    if(depth>8 || files_.size()>=256)throw std::runtime_error("Advertisement source exceeds resource limits");
    if(std::find(active_urls_.begin(),active_urls_.end(),url)!=active_urls_.end())
        throw std::runtime_error("Advertisement source contains a redirect cycle");
    active_urls_.push_back(url);
    struct Pop {std::vector<std::string>& stack;~Pop(){stack.pop_back();}} pop{active_urls_};
    const std::string path=prefix_+"_"+std::to_string(files_.size());
    files_.push_back(path); // Includes partial downloads in cleanup on exceptions.
    const std::string effective=download_(url,path);
    const std::string base=effective.empty() ? url : effective;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Could not read downloaded advertisement");
    input.seekg(0,std::ios::end);const auto size=input.tellg();input.seekg(0);
    if(size<=0)throw std::runtime_error("Advertisement download returned an empty response");
    total_bytes_+=static_cast<unsigned long long>(size);
    if(total_bytes_>256ULL*1024*1024)throw std::runtime_error("Advertisement source exceeds download limit");
    std::string body(static_cast<size_t>(std::min<std::streamoff>(size,1024*1024)), '\0');
    input.read(body.data(),static_cast<std::streamsize>(body.size()));input.close();
    std::string trimmed=Trim(body);
    if(trimmed.rfind("\xEF\xBB\xBF",0)==0)trimmed=Trim(trimmed.substr(3));
    const bool hls=trimmed.rfind("#EXTM3U",0)==0;
    const bool xml=!trimmed.empty() && trimmed.front()=='<';
    const bool mp4=body.size()>=8 && body.substr(4,4)=="ftyp";
    const bool webm=body.size()>=4 && static_cast<unsigned char>(body[0])==0x1a &&
        static_cast<unsigned char>(body[1])==0x45 && static_cast<unsigned char>(body[2])==0xdf &&
        static_cast<unsigned char>(body[3])==0xa3;
    const bool gzip=body.size()>=2 && static_cast<unsigned char>(body[0])==0x1f &&
        static_cast<unsigned char>(body[1])==0x8b;
    const bool json=!trimmed.empty() && (trimmed.front()=='{' || trimmed.front()=='[');
    const std::string kind=hls ? "hls" : xml ? "xml-or-html" : mp4 ? "mp4" :
        webm ? "webm" : gzip ? "gzip" : json ? "json" : "unknown";
    if(report_)report_("bytes="+std::to_string(size)+" kind="+kind);
    if((hls || xml) && size>1024*1024)throw std::runtime_error("Advertisement description is too large");
    if(xml) {
        tinyxml2::XMLDocument document;
        if(document.Parse(trimmed.data(),trimmed.size())!=tinyxml2::XML_SUCCESS)
            throw std::runtime_error("Advertisement URL returned invalid XML or an HTML page, not video");
        const auto root=document.RootElement();
        if(!root || std::string(root->Name())!="VAST")
            throw std::runtime_error("Advertisement URL returned a document instead of playable video");
        std::string media,wrapper;int score=0;FindVast(root,media,wrapper,score);
        const std::string next=!media.empty() ? media : wrapper;
        if(next.empty())throw std::runtime_error("VAST advertisement response contains no playable video (no-fill or unsupported media)");
        if(report_)report_("kind=vast resolved="+std::string(media.empty() ? "wrapper" : "video"));
        const auto result=Fetch(Resolve(base,next),depth+1);
        ready_[url]=result;return result;
    }
    if(hls) {
        // Offline preparation preserves playlist URLs/byte ranges and keys,
        // while HTTPS remains in HttpClient rather than the FFmpeg TLS backend.
        std::istringstream lines(trimmed);std::string line,rewritten;
        while(std::getline(lines,line)) {
            line=Trim(line);
            if(!line.empty() && line.front()!='#') {
                line=media_input::FfmpegLocalFileUrl(Fetch(Resolve(base,line),depth+1));
            } else {
                size_t cursor=0;
                while((cursor=line.find("URI=\"",cursor))!=std::string::npos) {
                    const auto start=cursor+5,end=line.find('"',start);
                    if(end==std::string::npos)throw std::runtime_error("Invalid advertisement playlist URI");
                    const auto local=media_input::FfmpegLocalFileUrl(Fetch(Resolve(base,line.substr(start,end-start)),depth+1));
                    line.replace(start,end-start,local);cursor=start+local.size()+1;
                }
            }
            rewritten+=line+"\n";
        }
        std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<rewritten;
        if(!output)throw std::runtime_error("Could not prepare advertisement playlist");
    }
    ready_[url]=path;
    return path;
}
} // namespace opennow
