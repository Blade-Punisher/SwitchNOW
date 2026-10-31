// Exercise the real HttpClient retry loop with a deterministic curl transport.
// One translation unit avoids MinGW duplicate dynamic TLS initializers in
// the existing header-only HttpCancellationScope.
#include "../app/src/http_client.cpp"
#include "http_socket_guard.hpp"
#include "runtime_journal.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <stdexcept>
using namespace opennow;
namespace opennow { const std::string& AppHomePath() { static const std::string path="build/host-tests"; return path; } }
namespace {
struct FakeCurl { network::SocketGuard* guard = nullptr; };
int calls = 0, failure_count = 0;
bool opened_before_error = false;
}
extern "C" {
CURL* curl_easy_init(void) { return reinterpret_cast<CURL*>(new FakeCurl); }
void curl_easy_cleanup(CURL* handle) {
    auto* fake = reinterpret_cast<FakeCurl*>(handle);
    assert(fake->guard); // Callback context must outlive easy cleanup.
    (void)fake->guard->trace.active();
    delete fake;
}
CURLcode curl_easy_setopt(CURL* handle, CURLoption option, ...) {
    va_list args; va_start(args, option);
    if (option == CURLOPT_OPENSOCKETDATA)
        reinterpret_cast<FakeCurl*>(handle)->guard = va_arg(args, network::SocketGuard*);
    va_end(args); return CURLE_OK;
}
CURLcode curl_easy_perform(CURL* handle) {
    ++calls;
    if (calls > failure_count) return CURLE_OK;
    auto& guard = *reinterpret_cast<FakeCurl*>(handle)->guard;
    guard.attempts = 1; guard.opened = opened_before_error ? 1 : 0;
    guard.error = ENOBUFS;
    network::ResourceFailure(ENOBUFS);
    // Advance the resource clock, keeping the real loop fast and deterministic.
    { std::lock_guard<std::mutex> lock(network::resource_mutex);
      network::resource_state.cooldown = network::Clock::now(); }
    return CURLE_COULDNT_CONNECT;
}
CURLcode curl_easy_getinfo(CURL*, CURLINFO info, ...) {
    va_list args; va_start(args, info);
    if (info == CURLINFO_PRIMARY_IP || info == CURLINFO_LOCAL_IP || info == CURLINFO_EFFECTIVE_URL)
        *va_arg(args, char**) = nullptr;
    else if (info == CURLINFO_NAMELOOKUP_TIME || info == CURLINFO_CONNECT_TIME ||
             info == CURLINFO_APPCONNECT_TIME || info == CURLINFO_STARTTRANSFER_TIME || info == CURLINFO_TOTAL_TIME)
        *va_arg(args, double*) = 0;
    else *va_arg(args, long*) = info == CURLINFO_RESPONSE_CODE ? 200 : 0;
    va_end(args); return CURLE_OK;
}
const char* curl_easy_strerror(CURLcode) { return "mock socket failure"; }
curl_slist* curl_slist_append(curl_slist*, const char*) { return nullptr; }
void curl_slist_free_all(curl_slist*) {}
}
namespace opennow {
void LogRuntimeEvent(const std::string&, const std::string&, const std::string&) {}
std::uint64_t BeginRuntimeOperation(const std::string&, const std::string&, const std::string&) { return 0; }
void EndRuntimeOperation(std::uint64_t, const std::string&, const std::string&, const std::string&, const std::string&) {}
std::string SanitizeRuntimeUrl(const std::string& url) { return url; }
}
int main() {
    HttpClient http;
    auto reset = [&] {
        calls = 0; opened_before_error = false;
        std::lock_guard<std::mutex> lock(network::resource_mutex);
        network::resource_state = {};
    };
    reset(); failure_count = 2;
    assert(http.Request("POST", "https://example.org/v2/session", "test").status_code == 200);
    assert(calls == 3 && network::launch_count == 0);
    reset(); failure_count = 99;
    bool failed = false;
    try { (void)http.Request("POST", "https://example.org/v2/session", "test"); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed && calls == 3 && network::launch_count == 0);
    reset(); failure_count = 1; opened_before_error = true; failed = false;
    try { (void)http.Request("POST", "https://example.org/v2/session", "test"); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed && calls == 1); // No replay after potentially sending a POST.
    reset(); failure_count = 1; failed = false;
    const auto cancelled = [&] { return calls != 0; };
    try { (void)http.Request("POST", "https://example.org/v2/session", "test", {}, {}, 10, 30, 0, {}, cancelled); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed && calls == 1 && network::launch_count == 0);
}
