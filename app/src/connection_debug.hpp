#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <functional>
namespace opennow::connection_debug {
struct Endpoint { std::string host, route; bool secure = false; };
Endpoint Describe(const std::string& url);
struct ResultInfo {
    int curl_code = 0, socket_attempts = 0, socket_failures = 0;
    long os_error = 0, http_status = 0, ssl_verify = 0;
    std::int64_t dns_us = 0, connect_us = 0, tls_us = 0, first_byte_us = 0, total_us = 0;
    std::string remote_ip, local_ip;
    long remote_port = 0, local_port = 0;
};
const char* Phase(const ResultInfo& result, bool secure, bool probe);
const char* MessageClass(const std::string& text);
const char* ErrnoName(int error);
using Fields = std::vector<std::pair<std::string,std::string>>;
bool Enabled() noexcept;
void Initialize(const std::string& home, const std::string& version) noexcept;
void Shutdown() noexcept;
// Export only: callback must read files, never call the logger recursively.
// Serializes writers, closes the persistent handle, and reopens it afterwards.
void CaptureWithWriterClosed(const std::function<void()>& capture);
void Event(std::uint64_t request, const char* event, const Fields& fields = {}) noexcept;
void SystemSnapshot(const char* reason, bool force = false) noexcept;
class Request {
public:
    Request(const std::string& method, const std::string& url, bool proxy, bool probe = false) noexcept;
    ~Request();
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    bool active() const { return id_ != 0; }
    void Socket(std::int64_t fd, int family, int type, int protocol,
                const std::string& ip, int port, int error, std::uint32_t bsd) noexcept;
    void Closed(std::int64_t fd, int result, int error, std::uint32_t bsd) noexcept;
    void Text(const std::string& text) noexcept;
    void Finish(ResultInfo result) noexcept;
private:
    std::uint64_t id_ = 0;
    bool secure_ = false, probe_ = false, completed_ = false;
    int attempts_ = 0, failures_ = 0;
    unsigned message_flags_ = 0;
};
}
