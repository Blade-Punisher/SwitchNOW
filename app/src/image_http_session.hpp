#pragma once
#include <functional>
#include <memory>
#include <string>
namespace opennow {
struct ImageDownload {
    long status = 0;
    std::string body;
    bool cancelled = false;
    long new_connections = 0;
    int curl_code = 0;
    long elapsed_ms = 0, dns_ms = 0, connect_ms = 0;
};
// One instance per image worker. All curl callback storage has session lifetime.
class ImageHttpSession {
public:
    ImageHttpSession();
    ~ImageHttpSession();
    ImageDownload Fetch(const std::string& url, size_t maximum_bytes,
        const std::function<bool()>& cancelled);
    void Reset();
    bool Active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> state_;
};
}
