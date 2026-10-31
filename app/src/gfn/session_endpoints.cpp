#include "session_endpoints.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace opennow::gfn::endpoints {
namespace {
std::string String(json_t* obj, const char* key) {
    auto* v=json_object_get(obj,key);
    return json_is_string(v) ? json_string_value(v) : "";
}
bool Host(const std::string& host) {
    if(host.size()>2 && host.front()=='[' && host.back()==']')
        return host.find_first_not_of("0123456789abcdefABCDEF:[]")==std::string::npos &&
            std::count(host.begin(),host.end(),':')>=2 && host.find(']')==host.size()-1 && host.find('[',1)==std::string::npos;
    return !host.empty() && host.size()<=253 &&
        host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-")==std::string::npos &&
        host.front()!='.' && host.back()!='.' && host.find("..") == std::string::npos;
}
std::string Authority(const std::string& url) {
    const auto start=url.find("://");
    if(start==std::string::npos || url.find_first_of(" \t\r\n\\")!=std::string::npos) return {};
    auto authority=url.substr(start+3,url.find_first_of("/?#",start+3)-(start+3));
    if(authority.empty()) return {};
    const auto colon=authority.front()=='[' ? authority.find(':',authority.find(']')) : authority.find(':');
    const auto host=authority.substr(0,colon);
    if(!Host(host)) return {};
    if(colon!=std::string::npos) {
        const auto port=authority.substr(colon+1);
        int parsed=0;const auto r=std::from_chars(port.data(),port.data()+port.size(),parsed);
        if(r.ec!=std::errc() || r.ptr!=port.data()+port.size() || parsed<=0 || parsed>65535) return {};
    }
    return authority;
}
std::string ConnectionHost(json_t* connection) {
    auto* ip=json_object_get(connection,"ip");
    if(json_is_array(ip)) {
        size_t i;json_t* entry;
        json_array_foreach(ip,i,entry) {
            if(json_is_string(entry) && Host(json_string_value(entry))) return json_string_value(entry);
        }
    }
    if(json_is_string(ip) && Host(json_string_value(ip))) return json_string_value(ip);
    const auto authority=Authority(String(connection,"resourcePath"));
    return authority.substr(0,authority.starts_with('[') ? authority.find(']')+1 : authority.find(':'));
}
bool NativeDescriptor(json_t* connection) {
    auto* protocol=json_object_get(connection,"appLevelProtocol");
    auto* usage=json_object_get(connection,"usage");
    return (json_is_integer(protocol) && (json_integer_value(protocol)==1 || json_integer_value(protocol)==6)) ||
        (json_is_integer(usage) && json_integer_value(usage)==16);
}
std::string Build(json_t* connection,const std::string& fallback,bool control_fallback=false) {
    const auto path=String(connection,"resourcePath");
    if(path.starts_with("wss://")) return ValidSignalingUrl(path) ? path : "";
    if(path.starts_with("https://")) {
        auto url="wss://"+path.substr(8);
        return ValidSignalingUrl(url) ? url : "";
    }
    auto host=ConnectionHost(connection);
    if(host.empty()) host=fallback;
    if(!Host(host)) return {};
    if(path.starts_with("rtsp://") || path.starts_with("rtsps://")) {
        const auto authority=Authority(path);
        if(authority.empty()) return {};
        const auto resource_host=authority.substr(0,authority.starts_with('[') ? authority.find(']')+1 : authority.find(':'));
        return "wss://"+resource_host+":443/nvst/";
    }
    const auto protocol=json_integer_value(json_object_get(connection,"appLevelProtocol"));
    const bool rtc=(protocol==4 || protocol==5) && !NativeDescriptor(connection);
    if((!path.empty() && path.front()=='/') || (path.empty() && rtc)) {
        // Preserve a dedicated WebRTC port. Native RTSP descriptors use the
        // default WSS gateway, not their advertised RTSP/control port.
        int port=443;
        auto* value=json_object_get(connection,"port");
        if(!control_fallback && !NativeDescriptor(connection) && value && !json_is_null(value)) {
            if(!json_is_integer(value) || json_integer_value(value)<1 || json_integer_value(value)>65535) return {};
            port=static_cast<int>(json_integer_value(value));
        }
        auto url="wss://"+host+":"+std::to_string(port)+(path.empty() ? "/" : path);
        return ValidSignalingUrl(url) ? url : "";
    }
    if(!path.empty()) return {};
    return "wss://"+host+":443/nvst/";
}
}
std::string ControlBaseUrl(const std::string& value) {
    if(value.empty() || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<=0x20 || c==0x7f;})) return {};
    const auto url=value.starts_with("https://") ? value :
        value.find("://")==std::string::npos ? "https://"+value : std::string{};
    if(url.empty() || url.find_first_of("?#")!=std::string::npos) return {};
    auto authority=Authority(url);
    if(authority.empty()) return {};
    const auto path_start=url.find('/',8);
    auto path=path_start==std::string::npos ? std::string("/") : url.substr(path_start);
    if(path.back()!='/') path+='/';
    std::transform(authority.begin(),authority.end(),authority.begin(),[](unsigned char c) {
        return c>='A' && c<='Z' ? static_cast<char>(c-'A'+'a') : static_cast<char>(c);
    });
    if(authority.ends_with(":443")) authority.resize(authority.size()-4);
    return "https://"+authority+path;
}
std::string SignInUrl(const std::string& base, const std::string& peer, const std::string& session_id, bool query_registration) {
    if(!ValidSignalingUrl(base) || peer.empty() || session_id.empty())
        throw std::runtime_error("Invalid signaling registration context");
    const auto encode=[](const std::string& value) {
        constexpr char hex[]="0123456789ABCDEF";
        std::string out;
        for(const unsigned char c:value) {
            if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
                c=='-' || c=='_' || c=='.' || c=='~') out+=static_cast<char>(c);
            else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
        }
        return out;
    };
    const auto clean=base.substr(0,base.find('#'));
    const auto query_pos=clean.find('?');
    auto path=clean.substr(0,query_pos);
    while(!path.empty() && path.back()=='/') path.pop_back();
    if(!path.ends_with("/sign_in")) path+="/sign_in";
    std::string query;
    if(query_pos!=std::string::npos) {
        const auto existing=clean.substr(query_pos+1);
        for(size_t begin=0;begin<existing.size();) {
            const auto end=existing.find('&',begin);
            const auto field=existing.substr(begin,end==std::string::npos ? end : end-begin);
            const auto key=field.substr(0,field.find('='));
            if(!field.empty() && key!="peer_id" && key!="pairing_id" && key!="version" && key!="peer_role")
                query+=field+"&";
            if(end==std::string::npos) break;
            begin=end+1;
        }
    }
    return path+"?"+query+"peer_id="+encode(peer)+"&version="+(query_registration ? "3.0" : "2")+
        "&peer_role=1&pairing_id="+encode(session_id);
}
bool ValidSignalingUrl(const std::string& url) {
    return url.starts_with("wss://") && !Authority(url).empty();
}
std::string SafeRoute(const std::string& url) {
    const auto authority=Authority(url);
    if(authority.empty()) return "invalid";
    const auto route_start=url.find('/',url.find("://")+3);
    const auto route=route_start==std::string::npos ? std::string{} :
        url.substr(route_start,url.find_first_of("?#",route_start)-route_start);
    // Do not export arbitrary path segments which may carry credentials.
    return authority+(route=="/nvst/" || route=="/nvst" || route=="/nvst/sign_in" ? route : "/[custom_path]");
}
namespace {
std::string EndpointKey(const std::string& url) {
    auto key=url;
    const auto authority=Authority(key);
    if(authority.ends_with(":443")) {
        const auto start=key.find("://")+3;
        key.erase(start+authority.size()-4,4);
    }
    return key;
}
}
std::string RecoveryEndpoint(const std::string& preferred,const std::vector<std::string>& candidates,
    const std::string& failed) {
    if(failed.empty() || EndpointKey(preferred)!=EndpointKey(failed)) return preferred;
    for(const auto& candidate:candidates)
        if(ValidSignalingUrl(candidate) && EndpointKey(candidate)!=EndpointKey(failed)) return candidate;
    return preferred;
}
Selection Resolve(json_t* session) {
    Selection out;
    if(!json_is_object(session)) return out;
    auto* connections=json_object_get(session,"connectionInfo");
    json_t* control=nullptr;
    size_t i;json_t* entry;
    json_array_foreach(connections,i,entry) {
        auto* usage=json_object_get(entry,"usage");
        if(!json_is_integer(usage)) continue;
        const auto number=json_integer_value(usage);
        if((number==14 || number==16) && out.descriptor_summaries.size()<8) {
            const auto path=String(entry,"resourcePath");
            const auto scheme=path.starts_with("wss://") ? "wss" : path.starts_with("https://") ? "https" :
                path.starts_with("rtsps://") ? "rtsps" : path.starts_with("rtsp://") ? "rtsp" :
                path.empty() ? "empty" : path.front()=='/' ? "relative" : "other";
            const auto numeric=[&](const char* key) {
                auto* value=json_object_get(entry,key);
                return json_is_integer(value) ? std::to_string(json_integer_value(value)) : "missing";
            };
            out.descriptor_summaries.push_back("usage="+std::to_string(number)+
                " app_protocol="+numeric("appLevelProtocol")+" port="+numeric("port")+" resource_kind="+scheme);
        }
        if(number==14) {
            ++out.usage14_count;
            if(!control || ConnectionHost(control).empty()) control=entry;
        } else if(number==16) ++out.usage16_count;
    }
    if(control) {
        out.control_host=ConnectionHost(control);
        if(!out.control_host.empty()) out.control_source="usage14";
    }
    if(out.control_host.empty()) {
        auto host=String(session,"serverIp");
        if(Host(host)) {out.control_host=host;out.control_source="serverIp";}
    }
    if(out.control_host.empty()) {
        out.control_host=ConnectionHost(json_object_get(session,"sessionControlInfo"));
        if(!out.control_host.empty()) out.control_source="sessionControlInfo";
    }
    struct Candidate {std::string url,source;int rank;};
    std::vector<Candidate> candidates;
    const auto add=[&](const std::string& url,const std::string& source,int rank) {
        if(!ValidSignalingUrl(url)) return;
        const auto key=EndpointKey(url);
        for(auto& c:candidates) if(EndpointKey(c.url)==key) {
            if(rank<c.rank) {
                c.source=source;c.rank=rank;
                std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.rank<b.rank;});
            }
            return;
        }
        candidates.push_back({url,source,rank});
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.rank<b.rank;});
        if(candidates.size()>8) candidates.pop_back();
    };
    auto explicit_url=String(session,"signalingUrl");
    if(explicit_url.starts_with("https://")) explicit_url="wss://"+explicit_url.substr(8);
    add(explicit_url,"explicit",0);
    // Prefer explicit WSS descriptors and dedicated WebRTC descriptors over
    // a native RTSP gateway. Inspect every usage=14/16, not just the first.
    for(int pass=0;pass<2;++pass) {
        json_array_foreach(connections,i,entry) {
            auto* usage=json_object_get(entry,"usage");
            if(!json_is_integer(usage) || (json_integer_value(usage)!=14 && json_integer_value(usage)!=16)) continue;
            const auto path=String(entry,"resourcePath");
            const bool explicit_path=path.starts_with("wss://") || path.starts_with("https://");
            const bool native=NativeDescriptor(entry) || path.starts_with("rtsp://") || path.starts_with("rtsps://");
            if((pass==0)!=explicit_path) continue;
            const auto source=std::string(json_integer_value(usage)==14 ? "usage14" : "usage16")+
                (explicit_path ? "_explicit" : native ? "_native_fallback" : "_webrtc");
            add(Build(entry,out.control_host),source,explicit_path ? 1 : native ? 3 : 2);
        }
    }
    if(candidates.empty() && !out.control_host.empty() &&
        (json_is_object(json_object_get(session,"sessionControlInfo")) || Host(String(session,"serverIp")))) add(Build(json_object_get(session,"sessionControlInfo"),out.control_host,true),"sessionControlInfo",4);
    for(const auto& c:candidates) out.signaling_candidates.push_back(c.url);
    if(!candidates.empty()) {
        out.signaling_url=candidates.front().url;
        out.signaling_source=candidates.front().source;
    }
    return out;
}
}
