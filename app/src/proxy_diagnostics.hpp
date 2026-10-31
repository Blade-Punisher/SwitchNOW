#pragma once
#include "stream_settings.hpp"
#include <cstdint>
#include <string>
namespace opennow::proxy_diagnostics {
void Configuration(const StreamSettings& settings);
class Request {
public:
    Request(const std::string& method, const std::string& target, const std::string& proxy);
    ~Request();
    void Finish(int curl_code, long http_status, long elapsed_ms, long connect_status = 0, long dns_ms = -1, long connect_ms = -1);
    bool Active() const { return id_ != 0; }
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
private:
    std::uint64_t id_ = 0;
    bool finished_ = false;
};
}
