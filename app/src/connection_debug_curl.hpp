#pragma once
#include "connection_debug.hpp"
#include <curl/curl.h>
#include <cerrno>
#include <cmath>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace opennow::connection_debug {
inline std::uint32_t LastBsd() noexcept {
#ifdef __SWITCH__
    return socketGetLastResult();
#else
    return 0;
#endif
}
inline curl_socket_t OpenSocket(void* data,curlsocktype,struct curl_sockaddr* address) noexcept {
    auto* request=static_cast<Request*>(data);
    const auto fd=::socket(address->family,address->socktype,address->protocol);
    const int saved=errno;
#ifdef _WIN32
    const int wsa=WSAGetLastError();
#endif
    const auto bsd=LastBsd();
    try {
        char ip[INET6_ADDRSTRLEN] {}; int port=0;
        if (address->family==AF_INET && address->addrlen>=sizeof(sockaddr_in)) {
            auto* a=reinterpret_cast<sockaddr_in*>(&address->addr);
            inet_ntop(AF_INET,&a->sin_addr,ip,sizeof(ip)); port=ntohs(a->sin_port);
        } else if (address->family==AF_INET6 && address->addrlen>=sizeof(sockaddr_in6)) {
            auto* a=reinterpret_cast<sockaddr_in6*>(&address->addr);
            inet_ntop(AF_INET6,&a->sin6_addr,ip,sizeof(ip)); port=ntohs(a->sin6_port);
        }
        request->Socket(fd==CURL_SOCKET_BAD ? -1 : static_cast<std::int64_t>(fd),address->family,
            address->socktype,address->protocol,ip,port,fd==CURL_SOCKET_BAD ? saved : 0,bsd);
    } catch (...) {}
#ifdef _WIN32
    WSASetLastError(wsa);
#endif
    errno=saved;
    return fd;
}
inline int CloseSocket(void* data,curl_socket_t fd) noexcept {
#ifdef _WIN32
    const int rc=closesocket(fd), saved=errno, wsa=WSAGetLastError();
#else
    const int rc=::close(fd), saved=errno;
#endif
    const auto bsd=LastBsd();
    static_cast<Request*>(data)->Closed(static_cast<std::int64_t>(fd),rc,rc<0?saved:0,bsd);
#ifdef _WIN32
    WSASetLastError(wsa);
#endif
    errno=saved;
    return rc;
}
inline int CurlText(CURL*,curl_infotype kind,char* data,size_t bytes,void* opaque) noexcept {
    const int saved=errno;
    try {
        if (kind==CURLINFO_TEXT && bytes<=4096)
            static_cast<Request*>(opaque)->Text(std::string(data,bytes));
    } catch (...) {}
    errno=saved;
    return 0;
}
inline void Attach(CURL* curl,Request& request) noexcept {
    if (!request.active()) return;
    curl_easy_setopt(curl,CURLOPT_OPENSOCKETFUNCTION,OpenSocket);
    curl_easy_setopt(curl,CURLOPT_OPENSOCKETDATA,&request);
    curl_easy_setopt(curl,CURLOPT_CLOSESOCKETFUNCTION,CloseSocket);
    curl_easy_setopt(curl,CURLOPT_CLOSESOCKETDATA,&request);
    curl_easy_setopt(curl,CURLOPT_DEBUGFUNCTION,CurlText);
    curl_easy_setopt(curl,CURLOPT_DEBUGDATA,&request);
    curl_easy_setopt(curl,CURLOPT_VERBOSE,1L);
}
inline void Finish(CURL* curl,Request& request,CURLcode code) noexcept {
    if (!request.active()) return;
    const int saved=errno;
    try {
        ResultInfo info; info.curl_code=code;
        // Capture system errno before any other diagnostics or NIFM call.
        curl_easy_getinfo(curl,CURLINFO_OS_ERRNO,&info.os_error);
        curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&info.http_status);
        curl_easy_getinfo(curl,CURLINFO_SSL_VERIFYRESULT,&info.ssl_verify);
        curl_easy_getinfo(curl,CURLINFO_PRIMARY_PORT,&info.remote_port);
        curl_easy_getinfo(curl,CURLINFO_LOCAL_PORT,&info.local_port);
        char* value=nullptr;
        if (curl_easy_getinfo(curl,CURLINFO_PRIMARY_IP,&value)==CURLE_OK && value) info.remote_ip=value;
        value=nullptr;
        if (curl_easy_getinfo(curl,CURLINFO_LOCAL_IP,&value)==CURLE_OK && value) info.local_ip=value;
        auto time=[&](CURLINFO field) { double seconds=0;curl_easy_getinfo(curl,field,&seconds);
            return static_cast<std::int64_t>(std::llround(seconds*1000000)); };
        info.dns_us=time(CURLINFO_NAMELOOKUP_TIME);info.connect_us=time(CURLINFO_CONNECT_TIME);
        info.tls_us=time(CURLINFO_APPCONNECT_TIME);info.first_byte_us=time(CURLINFO_STARTTRANSFER_TIME);
        info.total_us=time(CURLINFO_TOTAL_TIME);
        request.Finish(std::move(info));
    } catch (...) {}
    errno=saved;
}
}
