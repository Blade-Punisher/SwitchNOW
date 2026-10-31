#pragma once
#include "http_cancellation.hpp"

#include <string>
#include <vector>
#include <functional>

namespace opennow
{

struct HttpResponse
{
    long status_code = 0;
    std::string body;
    std::string etag;
    std::string last_modified;
};
struct TcpProbeDiagnostics {
    int curl_code = -1, dns_ms = -1, connect_ms = -1, tls_ms = -1;
    long os_error = 0;
    bool resource_exhausted = false, suppressed = false;
    std::string remote_ip, detail;
};

class HttpClient
{
  public:
    HttpResponse Request(
        const std::string& method,
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        const std::string& body = {},
        long connect_timeout_seconds = 10,
        long timeout_seconds = 30,
        size_t maximum_response_bytes = 0,
        const std::string& proxy_url = {},
        const std::function<bool()>& is_cancelled = {}) const;

    HttpResponse Get(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        long connect_timeout_seconds = 10,
        long timeout_seconds = 30,
        size_t maximum_response_bytes = 0) const;

    HttpResponse Post(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers,
        const std::string& body) const;

    void DownloadToFile(
        const std::string& url,
        const std::string& destination,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        const std::function<void(long long, long long)>& on_progress = {},
        const std::function<bool()>& is_cancelled = {},
        std::string* effective_url = nullptr) const;

    int MeasureTcpHandshakeLatencyMs(
        const std::string& url,
        long timeout_ms = 3000,
        TcpProbeDiagnostics* diagnostics = nullptr) const noexcept;
};

} // namespace opennow
