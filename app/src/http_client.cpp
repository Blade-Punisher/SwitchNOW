#ifdef __SWITCH__
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include "http_client.hpp"
#include "proxy_diagnostics.hpp"
#include "server_probe_policy.hpp"
#include "network_probe_budget.hpp"
#include "http_socket_guard.hpp"
#include "connection_debug_curl.hpp"

#include "runtime_journal.hpp"

#include <curl/curl.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <memory>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

namespace opennow
{
namespace
{

struct BodySink { std::string* body; size_t maximum; };
size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    if(size && nmemb > SIZE_MAX/size)return 0;
    const size_t bytes=size*nmemb;
    auto& sink=*static_cast<BodySink*>(userdata);
    if(sink.maximum && (sink.body->size()>sink.maximum || bytes>sink.maximum-sink.body->size()))return 0;
    try {sink.body->append(ptr,bytes);}catch(...){return 0;}
    return bytes;
}
size_t ReadArtworkHeaders(char* ptr,size_t size,size_t nmemb,void* userdata)
{
    if(size && nmemb>SIZE_MAX/size)return 0;
    const auto bytes=size*nmemb;
    auto& response=*static_cast<HttpResponse*>(userdata);
    try {
        std::string line(ptr,bytes);
        if(line.rfind("HTTP/",0)==0){response.etag.clear();response.last_modified.clear();return bytes;}
        const auto colon=line.find(':');if(colon==std::string::npos)return bytes;
        std::string name=line.substr(0,colon);
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
        auto value=line.substr(colon+1);const auto first=value.find_first_not_of(" \t");
        value=first==std::string::npos ? "" : value.substr(first);
        while(!value.empty() && (value.back()=='\r'||value.back()=='\n'||value.back()==' '))value.pop_back();
        if(value.size()<=1024 && value.find_first_of("\r\n")==std::string::npos) {
            if(name=="etag")response.etag=value;
            if(name=="last-modified")response.last_modified=value;
        }
    }catch(...){return 0;}
    return bytes;
}

bool IsArtworkRequest(const std::string& url)
{
    return url.find("img.nvidiagrid.net") != std::string::npos ||
           url.find("steamstatic.com") != std::string::npos ||
           url.find("akamaihd.net") != std::string::npos;
}

bool IsSessionPollRequest(
    const std::string& method, const std::string& url)
{
    if (method != "GET")
        return false;
    const auto marker = url.find("/v2/session/");
    return marker != std::string::npos &&
           marker + std::string("/v2/session/").size() < url.size();
}

struct DownloadProgressContext
{
    const std::function<void(long long, long long)>* on_progress = nullptr;
    const std::function<bool()>* is_cancelled = nullptr;
};

int DownloadProgress(
    void* userdata,
    curl_off_t total,
    curl_off_t downloaded,
    curl_off_t,
    curl_off_t)
{
    auto* context = static_cast<DownloadProgressContext*>(userdata);
    if (context->is_cancelled && *context->is_cancelled &&
        (*context->is_cancelled)())
        return 1;
    if (context->on_progress && *context->on_progress)
        (*context->on_progress)(
            static_cast<long long>(downloaded),
            static_cast<long long>(total));
    return 0;
}

} // namespace

HttpResponse HttpClient::Request(
    const std::string& method,
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    const std::string& body,
    long connect_timeout_seconds,
    long timeout_seconds,
    size_t maximum_response_bytes,
    const std::string& proxy_url,
    const std::function<bool()>& is_cancelled) const
{
    auto cancellation = is_cancelled;
    if (auto scoped = HttpCancellationScope::Callback()) {
        cancellation = [scoped, is_cancelled] {
            return scoped() || (is_cancelled && is_cancelled());
        };
    }
    if (cancellation && cancellation()) throw std::runtime_error("Request cancelled");
    std::unique_ptr<network::LaunchPriority> priority;
    if (url.find("/v2/session") != std::string::npos)
        priority = std::make_unique<network::LaunchPriority>();
    if (!network::WaitForResources(cancellation))
        throw std::runtime_error("Request cancelled or network resources still recovering");
    DownloadProgressContext progress {nullptr, &cancellation};
    const bool artwork_request = IsArtworkRequest(url);
    const bool session_poll_request = IsSessionPollRequest(method, url);
    const bool journal_request = !artwork_request && !session_poll_request;
    const auto started_at = std::chrono::steady_clock::now();
    const std::string safe_url = SanitizeRuntimeUrl(url);
    const std::uint64_t operation_id = journal_request
        ? BeginRuntimeOperation("http", method, "url=" + safe_url)
        : 0;

    proxy_diagnostics::Request proxy_trace(method,url,proxy_url);
    connection_debug::Request connection_trace(method,url,!proxy_url.empty());
    network::SocketGuard socket_guard {connection_trace};
    CURL* raw = curl_easy_init();
    if (!raw)
    {
        if (journal_request)
            EndRuntimeOperation(operation_id, "http", method, "error", "curl_easy_init_failed");
        throw std::runtime_error("curl_easy_init failed");
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, &curl_easy_cleanup);
    std::string response_body;
    BodySink body_sink {&response_body, maximum_response_bytes};
    HttpResponse response_headers;
    std::array<char, CURL_ERROR_SIZE> error_buffer {};

    curl_slist* raw_headers = nullptr;
    for (const auto& header : headers)
        raw_headers = curl_slist_append(raw_headers, header.c_str());

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(
        raw_headers,
        &curl_slist_free_all);

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    if (cancellation) {
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, DownloadProgress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &progress);
    }
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body_sink);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, ReadArtworkHeaders);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    // Explicitly disable environment proxies on direct routes; do not let
    // NO_PROXY silently bypass a proxy the user deliberately configured.
    curl_easy_setopt(curl.get(), CURLOPT_PROXY, proxy_url.c_str());
    if (!proxy_url.empty()) curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, "");

    if (header_list)
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());

    if (method == "POST")
    {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    else if (method != "GET")
    {
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty())
        {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
    }

    network::AttachGuard(curl.get(), socket_guard);
    CURLcode result = CURLE_OK;
    for (unsigned attempt = 0;; ++attempt) {
        socket_guard.attempts = socket_guard.opened = 0; socket_guard.error = 0;
        result = curl_easy_perform(curl.get());
        if (!network::SafeResourceRetry(result, socket_guard.error, socket_guard.opened, attempt)) break;
        LogRuntimeEvent("http", "resource_retry", "url=" + safe_url +
            " attempt=" + std::to_string(attempt + 1) + " socket_errno=" + std::to_string(socket_guard.error) +
            " sent=0 cooldown_ms=5000");
        connection_debug::Event(0, "resource_retry", {{"attempt", std::to_string(attempt + 1)},
            {"socket_errno", std::to_string(socket_guard.error)}, {"sent", "0"}});
        if (!network::WaitForResources(cancellation)) break;
        error_buffer.fill(0); response_body.clear();
    }
    connection_debug::Finish(curl.get(),connection_trace,result);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (proxy_trace.Active()) {
        long proxy_http_status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &proxy_http_status);
        long proxy_connect_status = 0; double proxy_dns = 0, proxy_connect = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_HTTP_CONNECTCODE, &proxy_connect_status);
        curl_easy_getinfo(curl.get(), CURLINFO_NAMELOOKUP_TIME, &proxy_dns);
        curl_easy_getinfo(curl.get(), CURLINFO_CONNECT_TIME, &proxy_connect);
        proxy_trace.Finish(static_cast<int>(result), proxy_http_status, elapsed_ms, proxy_connect_status,
            static_cast<long>(proxy_dns * 1000), static_cast<long>(proxy_connect * 1000));
    }
    if (result != CURLE_OK)
    {
        long verify_result = 0;
        char* primary_ip = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_SSL_VERIFYRESULT, &verify_result);
        curl_easy_getinfo(curl.get(), CURLINFO_PRIMARY_IP, &primary_ip);
        std::ostringstream detail;
        detail << "url=" << safe_url
               << " curl_code=" << static_cast<int>(result)
               << " curl_error=" << curl_easy_strerror(result)
               << " socket_errno=" << socket_guard.error
               << " socket_opened=" << socket_guard.opened
               << " ssl_verify=" << verify_result
               << " elapsed_ms=" << elapsed_ms;
        if (proxy_url.empty() && error_buffer[0] != '\0')
            detail << " backend_error=" << error_buffer.data();
        if (primary_ip && *primary_ip)
            detail << " primary_ip=" << primary_ip;

        if (journal_request)
            EndRuntimeOperation(operation_id, "http", method, "error", detail.str());
        else if (artwork_request)
            LogRuntimeEvent("http", "artwork_error", detail.str());
        else
            LogRuntimeEvent("http", "session_poll_error", detail.str());
        throw std::runtime_error(
            "HTTP " + method + " failed for " + safe_url + ": " +
            curl_easy_strerror(result));
    }

    long status_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status_code);
    if (journal_request)
    {
        EndRuntimeOperation(
            operation_id, "http", method, "ok",
            "url=" + safe_url +
            " status=" + std::to_string(status_code) +
            " bytes=" + std::to_string(response_body.size()) +
            " elapsed_ms=" + std::to_string(elapsed_ms));
    }

    return HttpResponse{
        .status_code = status_code,
        .body        = std::move(response_body),
        .etag        = std::move(response_headers.etag),
        .last_modified = std::move(response_headers.last_modified),
    };
}

HttpResponse HttpClient::Get(
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    long connect_timeout_seconds,
    long timeout_seconds,
    size_t maximum_response_bytes) const
{
    return Request(
        "GET", url, user_agent, headers, {},
        connect_timeout_seconds, timeout_seconds, maximum_response_bytes);
}

HttpResponse HttpClient::Post(
    const std::string& url,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    const std::string& body) const
{
    return Request("POST", url, user_agent, headers, body);
}

void HttpClient::DownloadToFile(
    const std::string& url,
    const std::string& destination,
    const std::string& user_agent,
    const std::vector<std::string>& headers,
    const std::function<void(long long, long long)>& on_progress,
    const std::function<bool()>& is_cancelled,
    std::string* effective_url) const
{
    FILE* output = std::fopen(destination.c_str(), "wb");
    if (!output)
        throw std::runtime_error("Could not create the advertisement media file");
    const auto close_file = [](FILE* handle) { std::fclose(handle); };
    std::unique_ptr<FILE, decltype(close_file)> file(output, close_file);

    connection_debug::Request connection_trace("DOWNLOAD",url,false);
    network::SocketGuard socket_guard {connection_trace};
    CURL* raw = curl_easy_init();
    if (!raw)
        throw std::runtime_error("curl_easy_init failed for media download");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, &curl_easy_cleanup);

    curl_slist* raw_headers = nullptr;
    for (const auto& header : headers)
        raw_headers = curl_slist_append(raw_headers, header.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(
        raw_headers, &curl_slist_free_all);
    std::array<char, CURL_ERROR_SIZE> error_buffer {};
    DownloadProgressContext progress {&on_progress, &is_cancelled};

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, file.get());
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, DownloadProgress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &progress);
    if (header_list)
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header_list.get());

    network::AttachGuard(curl.get(), socket_guard);
    const CURLcode result = curl_easy_perform(curl.get());
    connection_debug::Finish(curl.get(),connection_trace,result);
    if (effective_url) {
        char* final_url = nullptr;
        curl_easy_getinfo(curl.get(), CURLINFO_EFFECTIVE_URL, &final_url);
        *effective_url = final_url ? final_url : url;
    }
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    file.reset();
    if (result != CURLE_OK || status < 200 || status >= 300)
    {
        std::remove(destination.c_str());
        if (result == CURLE_ABORTED_BY_CALLBACK && is_cancelled && is_cancelled())
            throw std::runtime_error("Advertisement download cancelled");
        const std::string detail = error_buffer[0] != '\0'
            ? std::string(error_buffer.data())
            : std::string(curl_easy_strerror(result));
        throw std::runtime_error(
            "Advertisement download failed (HTTP " + std::to_string(status) +
            "): " + detail);
    }
}

int HttpClient::MeasureTcpHandshakeLatencyMs(
    const std::string& url,
    long timeout_ms, TcpProbeDiagnostics* diagnostics) const noexcept
try {
    if (diagnostics) *diagnostics = {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1L, timeout_ms));
    static network::ProbeBudget global_budget(1);
    network::ProbeLease lease(global_budget, deadline);
    if (!lease) {
        if (diagnostics) { diagnostics->curl_code = CURLE_OPERATION_TIMEDOUT; diagnostics->detail = "probe_slot_timeout"; }
        return -1;
    }
    timeout_ms = std::max(1L, static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count()));
    if (!network::TakeProbeSlot(deadline)) {
        if (diagnostics) { diagnostics->detail = "probe_suppressed_priority_rate_or_resource_budget"; diagnostics->suppressed = true; }
        return -1;
    }
    timeout_ms = std::max(1L, static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count()));
    connection_debug::Request connection_trace("PROBE",url,false,true);
    network::SocketGuard socket_guard {connection_trace, true};
    CURL* raw = curl_easy_init();
    if (!raw)
        return -1;

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(raw, &curl_easy_cleanup);
    const auto endpoint = server_probe::TcpEndpoint(url);
    if (endpoint.empty()) return -1;
    std::array<char, CURL_ERROR_SIZE> error {};
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error.data());
    curl_easy_setopt(curl.get(), CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_PROXY, "");
    curl_easy_setopt(curl.get(), CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

    network::AttachGuard(curl.get(), socket_guard);
    const auto probe_result = curl_easy_perform(curl.get());
    connection_debug::Finish(curl.get(),connection_trace,probe_result);
    if (diagnostics) {
        diagnostics->curl_code = static_cast<int>(probe_result);
        curl_easy_getinfo(curl.get(), CURLINFO_OS_ERRNO, &diagnostics->os_error);
        char* ip = nullptr;
        if (curl_easy_getinfo(curl.get(), CURLINFO_PRIMARY_IP, &ip) == CURLE_OK && ip)
            diagnostics->remote_ip = ip;
        if (socket_guard.error) diagnostics->os_error = socket_guard.error;
        diagnostics->resource_exhausted = network::ResourceError(socket_guard.error);
        diagnostics->detail = error.data();
        double dns = 0, connect = 0, tls = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_NAMELOOKUP_TIME, &dns);
        curl_easy_getinfo(curl.get(), CURLINFO_CONNECT_TIME, &connect);
        curl_easy_getinfo(curl.get(), CURLINFO_APPCONNECT_TIME, &tls);
        diagnostics->dns_ms = static_cast<int>(std::lround(dns * 1000));
        diagnostics->connect_ms = connect > 0 ? static_cast<int>(std::lround((connect - dns) * 1000)) : -1;
        diagnostics->tls_ms = tls > 0 ? static_cast<int>(std::lround((tls - connect) * 1000)) : -1;
    }
    if (probe_result != CURLE_OK) return -1;

    // A region estimate should reflect the network path, not DNS lookup or
    // variable TLS negotiation. TCP connect is approximately one network RTT.
    double lookup_seconds = 0.0;
    double connect_seconds = 0.0;
    if (curl_easy_getinfo(curl.get(), CURLINFO_NAMELOOKUP_TIME, &lookup_seconds) != CURLE_OK)
        return -1;
    if (curl_easy_getinfo(curl.get(), CURLINFO_CONNECT_TIME, &connect_seconds) != CURLE_OK)
        return -1;
    const double handshake_seconds = connect_seconds - lookup_seconds;
    if (handshake_seconds <= 0.0)
        return -1;

    return static_cast<int>(std::lround(handshake_seconds * 1000.0));
} catch (...) {
    // Optional latency measurement must never terminate the application when
    // a worker, diagnostics string or wait primitive cannot allocate resources.
    if (diagnostics) diagnostics->curl_code = CURLE_OUT_OF_MEMORY;
    return -1;
}

} // namespace opennow
