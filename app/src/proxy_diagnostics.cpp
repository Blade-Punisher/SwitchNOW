#include "proxy_diagnostics.hpp"
#include "proxy_policy.hpp"
#include "app_paths.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sys/stat.h>
namespace opennow::proxy_diagnostics {
namespace {
std::mutex mutex;
std::atomic<std::uint64_t> sequence{0};
std::string Endpoint(const std::string& raw) {
    const auto scheme=raw.find("://"); if(scheme==std::string::npos) return "invalid";
    const auto end=raw.find_first_of("/?#",scheme+3);
    auto authority=raw.substr(scheme+3,end==std::string::npos?std::string::npos:end-scheme-3);
    const auto at=authority.rfind('@'); if(at!=std::string::npos) authority.erase(0,at+1);
    const auto safe=proxy::NormalizeUrl(raw.substr(0,scheme+3)+authority);
    return safe.empty()?"invalid":safe;
}
void Write(const std::string& event) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex);
        const auto path=AppHomePath()+"/proxy.log";
        struct stat info{};
        if(stat(path.c_str(),&info)==0 && info.st_size>256*1024) {
            const auto previous=AppHomePath()+"/proxy.previous.log";
            std::remove(previous.c_str()); std::rename(path.c_str(),previous.c_str());
        }
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::ofstream out(path,std::ios::app|std::ios::binary);
        if(out) out << "PROXY ts_ms=" << ms << ' ' << event << '\n';
    } catch (...) {} // Diagnostics must never break or delay a session on SD errors.
}
}
void Configuration(const StreamSettings& settings) {
    Write("event=config enabled="+std::to_string(settings.proxy_enabled)+" endpoint="+
        (settings.proxy_url.empty()?"unset":Endpoint(settings.proxy_url))+
        " auth="+std::to_string(!settings.proxy_username.empty()||!settings.proxy_password.empty()));
}
Request::Request(const std::string& method,const std::string& target,const std::string& value) {
    if(value.empty()) return;
    id_=++sequence;
    // Never log arbitrary request bodies, target paths, credentials or curl debug text.
    const auto scheme=target.find("://");
    auto host=scheme==std::string::npos?std::string{}:target.substr(scheme+3);
    host=host.substr(0,host.find_first_of("/?#"));
    if(host.empty()||host.size()>256||host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-[]")!=std::string::npos) host="redacted";
    const auto verb=(method=="GET"||method=="POST"||method=="PUT"||method=="DELETE")?method:"request";
    Write("event=begin id="+std::to_string(id_)+" method="+verb+" target="+host+" endpoint="+Endpoint(value));
}
Request::~Request() { if(id_&&!finished_) Write("event=aborted id="+std::to_string(id_)); }
void Request::Finish(int code,long status,long elapsed,long connect_status,long dns_ms,long connect_ms) {
    if(!id_||finished_) return;
    finished_=true;
    Write("event=end id="+std::to_string(id_)+" curl_code="+std::to_string(code)+
        " http_status="+std::to_string(status)+" connect_status="+std::to_string(connect_status)+
        " dns_ms="+std::to_string(dns_ms)+" connect_ms="+std::to_string(connect_ms)+" elapsed_ms="+std::to_string(elapsed)+
        " result="+(code?"transport_error":(status>=400||connect_status>=400)?"http_error":"ok"));
}
}
