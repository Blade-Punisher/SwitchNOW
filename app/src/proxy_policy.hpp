#pragma once
#include "stream_settings.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
namespace opennow::proxy {
inline std::string NormalizeUrl(std::string_view raw) {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    if (raw.empty() || raw.size() > 512) return {};
    for (unsigned char c : raw) if (c <= 32 || c >= 127) return {};
    std::string value(raw);
    if (value.find("://") == std::string::npos) value = "http://" + value;
    const auto separator = value.find("://");
    auto scheme = value.substr(0, separator);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c){return std::tolower(c);});
    if (scheme != "http" && scheme != "socks5" && scheme != "socks5h") return {};
    auto authority = value.substr(separator + 3);
    if (!authority.empty() && authority.back() == '/') authority.pop_back();
    if (authority.empty() || authority.find_first_of("/@?#\\") != std::string::npos) return {};
    const auto colon = authority.rfind(':');
    if (colon == std::string::npos || colon == 0) return {};
    const auto host = authority.substr(0, colon);
    const auto port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5) return {};
    unsigned number = 0;
    for (unsigned char c : port) { if (!std::isdigit(c)) return {}; number = number*10 + c-'0'; }
    if (!number || number > 65535) return {};
    if (host.front() == '[') {
        if (host.size() < 4 || host.back() != ']' || host.find(':') == std::string::npos) return {};
        for (size_t i=1; i+1<host.size(); ++i)
            if (!std::isxdigit(static_cast<unsigned char>(host[i])) && host[i]!=':' && host[i]!='.') return {};
    } else {
        if (host.front()=='.' || host.front()=='-' || host.back()=='.' || host.back()=='-' || host.find("..")!=std::string::npos) return {};
        for (unsigned char c : host) if (!std::isalnum(c) && c!='.' && c!='-') return {};
    }
    return scheme + "://" + host + ":" + std::to_string(number);
}
inline bool ValidCredential(std::string_view value) {
    if (value.size()>256) return false;
    for (unsigned char c:value) if (c<32 || c==127) return false;
    return true;
}
inline std::string Encode(std::string_view value) {
    constexpr char hex[]="0123456789ABCDEF"; std::string result;
    for (unsigned char c:value) {
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') result+=char(c);
        else { result+='%';result+=hex[c>>4];result+=hex[c&15]; }
    }
    return result;
}
inline std::string EnabledUrl(const StreamSettings& settings) {
    if (!settings.proxy_enabled) return {};
    auto value=NormalizeUrl(settings.proxy_url);
    if (value.empty() || !ValidCredential(settings.proxy_username) || !ValidCredential(settings.proxy_password)) return {};
    if (!settings.proxy_username.empty() || !settings.proxy_password.empty()) {
        const auto start=value.find("://")+3;
        value.insert(start,Encode(settings.proxy_username)+":"+Encode(settings.proxy_password)+"@");
    }
    return value;
}
} // namespace opennow::proxy
