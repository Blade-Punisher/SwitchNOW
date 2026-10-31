#include "proxy_policy.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace opennow::proxy;
    for (const auto& scheme : {"http", "socks5", "socks5h"}) {
        const auto url = std::string(scheme) + "://proxy.example:1080";
        assert(NormalizeUrl(url) == url);
        opennow::StreamSettings settings;
        settings.proxy_url=url;
        assert(EnabledUrl(settings).empty());
        settings.proxy_enabled=true;
        assert(EnabledUrl(settings)==url);
        settings.proxy_username="user@a:b"; settings.proxy_password="p%:/?# word";
        assert(EnabledUrl(settings)==std::string(scheme)+"://user%40a%3Ab:p%25%3A%2F%3F%23%20word@proxy.example:1080");
    }
    assert(NormalizeUrl("  localhost:8080 ")=="http://localhost:8080");
    assert(NormalizeUrl("SOCKS5H://[::1]:1080/")=="socks5h://[::1]:1080");
    for (const auto* invalid : {"", "host", "http://host:0", "http://host:65536", "http://host:-1", "http://host:1x", "http://host:8080/path", "http://u:p@host:80", "http://host:80?x", "http://host:80#x", "file://host:80", "https://host:443", "socks://host:80", "http://bad host:80", "http://[bad]:80", "http://::1:80", "http://host:80\r\nX:1"}) assert(NormalizeUrl(invalid).empty());
    opennow::StreamSettings settings;settings.proxy_enabled=true;settings.proxy_url="http://host:80";settings.proxy_password="bad\nvalue";
    assert(EnabledUrl(settings).empty());
    assert(!ValidCredential(std::string(257,'a')));
    assert(ValidCredential("пароль"));
    std::cout << "Custom proxy endpoint/IPv6/credentials/encoding/disabled policy tests passed.\n";
}
