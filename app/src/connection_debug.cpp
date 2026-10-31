#include "connection_debug.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#ifdef __SWITCH__
#include <switch.h>
extern "C" {
#include <switch/services/bsd.h>
}
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace opennow::connection_debug {
namespace {
std::mutex writer;
std::string directory;
std::FILE* output = nullptr;
std::uint64_t output_bytes = 0;
std::atomic<bool> initialized {false};
std::atomic<std::uint64_t> sequence {0}, requests {0}, open_sockets {0}, snapshot_at {0};
// socketInitialize runs before main: use constant-initialized numeric storage,
// no logging, allocation or service calls from the link wrapper itself.
#ifdef __SWITCH__
constinit std::atomic<std::uint32_t> init_calls {0}, init_result {0xffffffffu};
constinit std::atomic<std::uint32_t> init_sessions {0}, init_efficiency {0}, init_service {0};
#endif
struct PreserveErrno { int value = errno; ~PreserveErrno() { errno = value; } };
std::uint64_t SteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string Atom(std::string text) {
    if (text.size() > 180) text.resize(180);
    for (auto& c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
              (u >= '0' && u <= '9') || std::string("._-:/[]").find(c) != std::string::npos)) c = '_';
    }
    return text;
}
}
Endpoint Describe(const std::string& url) {
    Endpoint result;
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) return {"invalid", "other", false};
    result.secure = url.substr(0,scheme) == "https" || url.substr(0,scheme) == "wss";
    const auto start = scheme + 3;
    const auto end = url.find_first_of("/?#",start);
    auto authority = url.substr(start,end-start);
    if (const auto at = authority.rfind('@'); at != std::string::npos) authority.erase(0,at+1);
    result.host = Atom(authority);
    const auto path_end = url.find_first_of("?#",end == std::string::npos ? url.size() : end);
    const auto path = end != std::string::npos && url[end] == '/'
        ? url.substr(end,path_end-end) : "";
    result.route = path == "/v2/serverInfo" ? "serverInfo" :
        path == "/v2/session" ? "sessions" : path.starts_with("/v2/session/") ? "session_by_id" :
        path.find("/gfn/queue") != std::string::npos ? "public_queue" : "other";
    return result;
}
const char* Phase(const ResultInfo& r, bool secure, bool probe) {
    if (r.http_status > 0) return r.curl_code == 0 ? "http_response" : "http_transfer";
    if (!r.curl_code) return probe ? "tcp_connected" : "completed_no_http_status";
    if (r.curl_code == 5 || r.curl_code == 6) return "dns";
    if (r.socket_attempts > 0 && r.socket_failures == r.socket_attempts) return "socket_create";
    if (r.curl_code == 7) return "tcp_connect";
    if (r.curl_code == 42) return "cancelled";
    if (r.tls_us > 0) return "after_tls";
    if (r.curl_code == 35 || r.curl_code == 51 || r.curl_code == 58 ||
        r.curl_code == 60 || r.curl_code == 77 || r.curl_code == 83) return "tls";
    if (secure && r.connect_us > 0) return "tls_or_before_http";
    if (r.socket_attempts > 0) return "after_dns_before_tcp_completed";
    return "before_socket_or_dns";
}
const char* MessageClass(const std::string& text) {
    // Never save libcurl's raw text: it can contain URLs or authentication.
    std::string lower = text;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){ return std::tolower(c); });
    if (lower.find("could not resolve") != std::string::npos || lower.find("couldn't resolve") != std::string::npos) return "dns_failed";
    if (lower.find("trying ") != std::string::npos) return "tcp_attempt";
    if (lower.find("connected to ") != std::string::npos) return "tcp_connected";
    if (lower.find("connection refused") != std::string::npos) return "connection_refused";
    if (lower.find("network is unreachable") != std::string::npos) return "network_unreachable";
    if (lower.find("no route to host") != std::string::npos) return "host_unreachable";
    if (lower.find("failed to connect") != std::string::npos || lower.find("immediate connect fail") != std::string::npos) return "tcp_failed";
    if (lower.find("certificate") != std::string::npos && lower.find("failed") != std::string::npos) return "certificate_failed";
    if (lower.find("ssl connection using") != std::string::npos) return "tls_connected";
    if (lower.find("timed out") != std::string::npos) return "timeout";
    if (lower.find("proxy env") != std::string::npos) return "environment_proxy_present";
    return nullptr;
}
const char* ErrnoName(int e) {
    switch(e) {
        case 0: return "none";
        case EMFILE: return "EMFILE"; case ENFILE: return "ENFILE";
        case ENOMEM: return "ENOMEM"; case ENOBUFS: return "ENOBUFS";
        case ECONNREFUSED: return "ECONNREFUSED"; case ENETUNREACH: return "ENETUNREACH";
        case EHOSTUNREACH: return "EHOSTUNREACH"; case ETIMEDOUT: return "ETIMEDOUT";
        case EACCES: return "EACCES"; case EPERM: return "EPERM";
        case EBADF: return "EBADF"; case EINVAL: return "EINVAL";
        default: return "other";
    }
}
bool Enabled() noexcept {
#if defined(SWITCHNOW_NETWORK_DEBUG) && SWITCHNOW_NETWORK_DEBUG
    return initialized.load(std::memory_order_acquire);
#else
    return false;
#endif
}
void Event(std::uint64_t id, const char* event, const Fields& fields) noexcept {
    PreserveErrno keep;
    if (!Enabled()) return;
    try {
        std::lock_guard<std::mutex> lock(writer);
        const auto path = directory + "/connection_debug.log";
        if (output_bytes >= 1024*1024) {
            if (output) { std::fclose(output); output=nullptr; }
            const auto previous=directory+"/connection_debug.previous.log";
            std::remove(previous.c_str()); std::rename(path.c_str(),previous.c_str());
            output_bytes=0;
        }
        if (!output) output=std::fopen(path.c_str(),"ab");
        const auto system = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (output) {
            std::ostringstream line;
            line << "NETDBG system_ms=" << system << " steady_ms=" << SteadyMs() << " request=" << id
                << " event=" << Atom(event);
            for (const auto& [key,value] : fields) line << ' ' << Atom(key) << '=' << Atom(value);
            line << '\n';
            const auto text=line.str();
            output_bytes+=std::fwrite(text.data(),1,text.size(),output);
            std::fflush(output); // preserve a complete line if the app is force-closed
        }
    } catch (...) {}
}
void Initialize(const std::string& home, const std::string& version) noexcept {
    PreserveErrno keep;
#if defined(SWITCHNOW_NETWORK_DEBUG) && SWITCHNOW_NETWORK_DEBUG
    try {
        { std::lock_guard<std::mutex> lock(writer);
          if (output) { std::fclose(output); output=nullptr; }
          directory = home; output_bytes=0;
          const auto path = directory + "/connection_debug.log", previous = directory + "/connection_debug.previous.log";
          struct stat info {};
          if (stat(path.c_str(),&info) == 0) { std::remove(previous.c_str()); std::rename(path.c_str(),previous.c_str()); }
          initialized.store(true,std::memory_order_release); }
        Event(0,"boot",{{"version",version},{"mode","automatic_network_debug"},{"payload_logging","disabled"}});
        SystemSnapshot("startup",true);
#ifdef __SWITCH__
        for (int type : {SOCK_STREAM,SOCK_DGRAM}) {
            const int fd = socket(AF_INET,type,0), error = fd < 0 ? errno : 0;
            const auto bsd = socketGetLastResult();
            Event(0,"socket_sanity",{{"type",std::to_string(type)},{"fd",std::to_string(fd)},
                {"errno",std::to_string(error)},{"errno_name",ErrnoName(error)},{"bsd_result",std::to_string(bsd)}});
            if (fd >= 0) close(fd);
        }
#endif
    } catch (...) {}
#else
    (void)home; (void)version;
#endif
}
void CaptureWithWriterClosed(const std::function<void()>& capture) {
    PreserveErrno keep;
    std::lock_guard<std::mutex> lock(writer);
    const auto path = directory + "/connection_debug.log";
    const bool reopen = output != nullptr;
    if (output) { std::fclose(output); output = nullptr; }
    struct Reopen {
        bool needed;
        const std::string& path;
        ~Reopen() { if (needed) output = std::fopen(path.c_str(), "ab"); }
    } restore {reopen, path};
    capture();
}
void Shutdown() noexcept {
    PreserveErrno keep;
    Event(0,"shutdown");
    try {
        std::lock_guard<std::mutex> lock(writer);
        initialized.store(false,std::memory_order_release);
        if (output) { std::fclose(output); output=nullptr; }
    } catch (...) {}
}
void SystemSnapshot(const char* reason, bool force) noexcept {
    PreserveErrno keep;
    if (!Enabled()) return;
    const auto now = SteadyMs();
    auto before = snapshot_at.load();
    if (!force && now >= before && now-before < 5000) return;
    if (!snapshot_at.compare_exchange_strong(before,now) && !force) return;
    try {
        Fields fields {{"reason",reason},{"requests_inflight",std::to_string(requests.load())},
            {"curl_sockets_tracked",std::to_string(open_sockets.load())}};
#ifdef __SWITCH__
        fields.emplace_back("socket_init_calls",std::to_string(init_calls.load()));
        fields.emplace_back("socket_init_rc",std::to_string(init_result.load()));
        fields.emplace_back("socket_init_sessions",std::to_string(init_sessions.load()));
        fields.emplace_back("socket_init_efficiency",std::to_string(init_efficiency.load()));
        fields.emplace_back("socket_init_service",std::to_string(init_service.load()));
        const auto last_bsd = socketGetLastResult();
        fields.emplace_back("bsd_last_result",std::to_string(last_bsd));
        fields.emplace_back("bsd_active",serviceIsActive(bsdGetServiceSession()) ? "1" : "0");
        fields.emplace_back("applet_type",std::to_string(appletGetAppletType()));
        fields.emplace_back("hos_version",std::to_string(hosversionGet()));
        NifmInternetConnectionType type {}; NifmInternetConnectionStatus status {}; u32 strength=0;
        auto rc = nifmGetInternetConnectionStatus(&type,&strength,&status);
        fields.emplace_back("nifm_rc",std::to_string(rc)); fields.emplace_back("nifm_status",std::to_string(status));
        fields.emplace_back("connection_type",std::to_string(type)); fields.emplace_back("signal_strength",std::to_string(strength));
        u32 ip=0,mask=0,gateway=0,dns1=0,dns2=0;
        rc = nifmGetCurrentIpConfigInfo(&ip,&mask,&gateway,&dns1,&dns2);
        fields.emplace_back("ip_config_rc",std::to_string(rc));
        auto format = [](u32 value) { const auto* b=reinterpret_cast<const unsigned char*>(&value);
            return std::to_string(b[0])+"."+std::to_string(b[1])+"."+std::to_string(b[2])+"."+std::to_string(b[3]); };
        fields.emplace_back("local_ip",format(ip)); fields.emplace_back("gateway",format(gateway));
        fields.emplace_back("dns1",format(dns1)); fields.emplace_back("dns2",format(dns2));
        u64 used=0,total=0;
        fields.emplace_back("memory_total_rc",std::to_string(svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0)));
        fields.emplace_back("memory_used_rc",std::to_string(svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0)));
        fields.emplace_back("process_memory_used",std::to_string(used)); fields.emplace_back("process_memory_total",std::to_string(total));
#endif
        Event(0,"system_snapshot",fields);
    } catch (...) {}
}
Request::Request(const std::string& method,const std::string& url,bool proxy,bool probe) noexcept {
    PreserveErrno keep;
    if (!Enabled()) return;
    try {
        const auto endpoint = Describe(url); secure_=endpoint.secure; probe_=probe;
        id_=sequence.fetch_add(1)+1; ++requests;
        Event(id_,"begin",{{"method",method},{"host",endpoint.host},{"route",endpoint.route},
            {"explicit_proxy",proxy?"1":"0"},{"kind",probe?"tcp_probe":"http"}});
    } catch (...) {}
}
Request::~Request() {
    PreserveErrno keep;
    if (id_) {
        if (!completed_) Event(id_,"unfinished",{{"reason","exited_before_result"}});
        --requests;
    }
}
void Request::Socket(std::int64_t fd,int family,int type,int protocol,const std::string& ip,int port,int error,std::uint32_t bsd) noexcept try {
    PreserveErrno keep;
    if (!id_) return;
    ++attempts_; failures_ += fd < 0;
    if (fd >= 0) ++open_sockets;
    Event(id_,"socket_open",{{"fd",std::to_string(fd)},{"family",std::to_string(family)},
        {"type",std::to_string(type)},{"protocol",std::to_string(protocol)},{"remote_ip",ip},
        {"remote_port",std::to_string(port)},{"errno",std::to_string(error)},
        {"errno_name",ErrnoName(error)},{"bsd_result",std::to_string(bsd)},
        {"curl_sockets_tracked",std::to_string(open_sockets.load())}});
} catch (...) {}
void Request::Closed(std::int64_t fd,int result,int error,std::uint32_t bsd) noexcept try {
    PreserveErrno keep;
    if (!id_) return;
    if (result == 0 && open_sockets.load()) --open_sockets;
    Event(id_,"socket_close",{{"fd",std::to_string(fd)},{"result",std::to_string(result)},
        {"errno",std::to_string(error)},{"errno_name",ErrnoName(error)},
        {"bsd_result",std::to_string(bsd)},{"curl_sockets_tracked",std::to_string(open_sockets.load())}});
} catch (...) {}
void Request::Text(const std::string& text) noexcept {
    PreserveErrno keep;
    try {
        const auto* kind=MessageClass(text); if (!kind) return;
        unsigned index=0;
        for (const auto* known : {"dns_failed","tcp_attempt","tcp_connected","connection_refused","network_unreachable",
            "host_unreachable","tcp_failed","certificate_failed","tls_connected","timeout","environment_proxy_present"}) {
            if (std::string(kind)==known) break;
            ++index;
        }
        const auto flag=1u<<index;
        if (!(message_flags_&flag)) { message_flags_|=flag; Event(id_,"progress",{{"stage",kind}}); }
    } catch (...) {}
}
void Request::Finish(ResultInfo result) noexcept {
    PreserveErrno keep;
    if (!id_) return;
    try {
        completed_=true; result.socket_attempts=attempts_; result.socket_failures=failures_;
        Event(id_,"result",{{"phase",Phase(result,secure_,probe_)},{"curl_code",std::to_string(result.curl_code)},
            {"os_errno",std::to_string(result.os_error)},{"errno_name",ErrnoName(result.os_error)},
            {"http_status",std::to_string(result.http_status)},{"ssl_verify",std::to_string(result.ssl_verify)},
            {"socket_attempts",std::to_string(attempts_)},{"socket_failures",std::to_string(failures_)},
            {"dns_us",std::to_string(result.dns_us)},{"tcp_completed_us",std::to_string(result.connect_us)},
            {"tls_completed_us",std::to_string(result.tls_us)},{"first_byte_us",std::to_string(result.first_byte_us)},
            {"total_us",std::to_string(result.total_us)},{"remote_ip",result.remote_ip},{"remote_port",std::to_string(result.remote_port)},
            {"local_ip",result.local_ip},{"local_port",std::to_string(result.local_port)}});
        if (result.curl_code || result.http_status >= 400) SystemSnapshot("request_failed");
    } catch (...) {}
}
}

#if defined(__SWITCH__) && defined(SWITCHNOW_NETWORK_DEBUG) && SWITCHNOW_NETWORK_DEBUG
namespace opennow::connection_debug {
void RecordSocketInitialization(Result result,const SocketInitConfig* config) noexcept {
    init_result.store(result,std::memory_order_relaxed);
    if (config) {
        init_sessions.store(config->num_bsd_sessions,std::memory_order_relaxed);
        init_efficiency.store(config->sb_efficiency,std::memory_order_relaxed);
        init_service.store(config->bsd_service_type,std::memory_order_relaxed);
    }
    init_calls.fetch_add(1,std::memory_order_release);
}
}
extern "C" Result __real_socketInitialize(const SocketInitConfig* config);
extern "C" Result __wrap_socketInitialize(const SocketInitConfig* config) {
    const auto* observed=config ? config : socketGetDefaultInitConfig();
    const Result result=__real_socketInitialize(config);
    const int saved=errno;
    opennow::connection_debug::RecordSocketInitialization(result,observed);
    errno=saved;
    return result;
}
#endif
