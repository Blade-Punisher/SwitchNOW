#include "image_http_session.hpp"
#include "network_resource_policy.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace opennow {
struct ImageHttpSession::Impl {
    CURL* curl = nullptr;
    std::string bytes;
    size_t maximum = 0;
    std::function<bool()> cancelled;
    bool stopped = false;
    unsigned opened = 0;
    int socket_error = 0;
    curl_slist* headers = nullptr;
    bool Stop() { stopped = stopped || (cancelled && cancelled()); return stopped; }
    static size_t Write(char* data,size_t size,size_t count,void* opaque) noexcept {
        try {
        auto& self=*static_cast<Impl*>(opaque);
        if(size && count>std::numeric_limits<size_t>::max()/size) return 0;
        const auto length=size*count;
        if(self.Stop() || length>self.maximum || self.bytes.size()>self.maximum-length)return 0;
        self.bytes.append(data,length);return length;
        } catch (...) { return 0; }
    }
    static int Progress(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept {
        try { return static_cast<Impl*>(opaque)->Stop()?1:0; } catch (...) { return 1; }
    }
    static curl_socket_t Open(void* opaque,curlsocktype,curl_sockaddr* address) noexcept {
        try {
        auto& self=*static_cast<Impl*>(opaque);
        if(self.Stop()){errno=ECANCELED;return CURL_SOCKET_BAD;}
        const auto socket=::socket(address->family,address->socktype,address->protocol);
        const int saved=errno;
        if(socket==CURL_SOCKET_BAD) {
#ifdef _WIN32
            self.socket_error=WSAGetLastError()==WSAENOBUFS?ENOBUFS:saved;
#else
            self.socket_error=saved;
#endif
            network::ResourceFailure(self.socket_error);
        } else ++self.opened;
        errno=saved;return socket;
        } catch (...) { errno=ECANCELED;return CURL_SOCKET_BAD; }
    }
    ~Impl(){if(curl)curl_easy_cleanup(curl);if(headers)curl_slist_free_all(headers);}
};
ImageHttpSession::ImageHttpSession() = default;
ImageHttpSession::~ImageHttpSession() = default;
void ImageHttpSession::Reset(){state_.reset();}
bool ImageHttpSession::Active() const {return bool(state_);}
ImageDownload ImageHttpSession::Fetch(const std::string& url,size_t maximum,const std::function<bool()>& cancelled) {
    ImageDownload output;
    const std::function<bool()> stop=[&]{return cancelled&&cancelled();};
    if(stop() || !network::WaitForResources(stop)){output.cancelled=true;Reset();return output;}
    if(!state_)state_=std::make_unique<Impl>();
    auto& state=*state_;
    if(!state.curl)state.curl=curl_easy_init();
    if(!state.curl)return output;
    // reset retains libcurl's connections/DNS/TLS caches, not request options.
    curl_easy_reset(state.curl);
    state.bytes.clear();state.maximum=maximum;state.cancelled=cancelled;state.stopped=false;
    if(!state.headers)state.headers=curl_slist_append(nullptr,"Accept: image/jpeg,image/png,image/*,*/*;q=0.8");
    curl_easy_setopt(state.curl,CURLOPT_HTTPHEADER,state.headers);
    auto* curl=state.curl;
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"SwitchNOW-images");
    curl_easy_setopt(curl,CURLOPT_PROXY,"");
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,5L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,15L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_MAXCONNECTS,1L);
    curl_easy_setopt(curl,CURLOPT_DNS_CACHE_TIMEOUT,120L);
    curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,long(CURLPROTO_HTTP|CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,Impl::Write);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&state);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,Impl::Progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&state);
    curl_easy_setopt(curl,CURLOPT_OPENSOCKETFUNCTION,Impl::Open);
    curl_easy_setopt(curl,CURLOPT_OPENSOCKETDATA,&state);
    const auto start=std::chrono::steady_clock::now();
    CURLcode code=CURLE_OK;
    for(unsigned attempt=0;;++attempt){
        state.opened=0;state.socket_error=0;
        code=curl_easy_perform(curl);
        if(!network::SafeResourceRetry(code,state.socket_error,state.opened,attempt))break;
        if(!network::WaitForResources(stop)){code=CURLE_ABORTED_BY_CALLBACK;break;}
        state.bytes.clear();
    }
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&output.status);
    curl_easy_getinfo(curl,CURLINFO_NUM_CONNECTS,&output.new_connections);
    output.cancelled=state.Stop()||code==CURLE_ABORTED_BY_CALLBACK;
    if(code==CURLE_OK && !output.cancelled)output.body=std::move(state.bytes);
    output.curl_code=static_cast<int>(code);output.elapsed_ms=static_cast<long>(elapsed);
    double dns=0,connect=0;curl_easy_getinfo(curl,CURLINFO_NAMELOOKUP_TIME,&dns);curl_easy_getinfo(curl,CURLINFO_CONNECT_TIME,&connect);
    output.dns_ms=static_cast<long>(dns*1000);output.connect_ms=static_cast<long>(connect*1000);
    state.cancelled={};state.bytes.clear();
    if(output.cancelled || code!=CURLE_OK)Reset();
    return output;
}
}
