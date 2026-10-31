#include "diagnostic_report.hpp"
#include "connection_debug.hpp"
#include "app_version.hpp"
#include "performance_recorder.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace opennow::report {
namespace {
std::uint32_t Crc(const std::string& text) {
    std::uint32_t value = 0xffffffffu;
    for (unsigned char ch : text) {
        value ^= ch;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
void U16(std::ostream& out, unsigned value) {
    out.put(static_cast<char>(value)); out.put(static_cast<char>(value >> 8));
}
void U32(std::ostream& out, std::uint32_t value) {
    U16(out, value & 65535); U16(out, value >> 16);
}
}

std::string FilterLog(const std::string& text) {
    static const std::regex endpoint(R"((https?|wss?)://[^\s]+)", std::regex::icase);
    static const std::regex jwt(R"([A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{12,}\.[A-Za-z0-9_-]{8,})");
    static const std::regex email(R"([A-Za-z0-9._+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    static const auto allowed = std::to_array<const char*>({
        "category=keys ", "category=form_input ", "category=image ", "category=gallery ", "category=description ", "PROXY ", "FORWARDER ", "category=shortcut ", "INPUT_SEND ", "INGRESS ", "NETDBG ", "STREAM diag ", "ATTEMPT ", "SIGNAL_DIAG ", "SIGNAL envelope ", "SIGNAL payload ", "SESSION start ", "SESSION stop", "SESSION error ",
        "PEER state ", "SIGNAL connected", "SIGNAL disconnected", "KEYFRAME ",
        "LIBPEER ", "SCTP ", "RECOVERY ", "POLL parsed:", "START parsed:", "AD update accepted:",
        "category=http ", "category=app ", "category=recovery ", "session_metrics ",
        "AUDOUT ", "STATS ", "LEVEL ", "SUMMARY rx=", "HANDSHAKE complete ", "HANDSHAKE reactivated ",
        "HEARTBEAT tx count=", "DCEP reliableChannelRequested=", "REGION ", "ROUTING ",
        "START region=", "LIVE_PAUSE ", "PAUSE complete ", "ADAPT ", "DATA ", "VIDEO ", "TRANSPORT ",
        "DECODE ", "RTP ", "UI performance_assessment=", "UI stream_end_evidence ", "SESSION summary ", "INPUT summary "});
    static const std::regex token_presence(R"(\bsessionToken=(present|missing)(?=\s|$))");
    static const std::regex cookie_event(R"(\b(internal_cookie_echo|internal_cookie_ack|connected_by=cookie_echo|connected_by=cookie_ack)\b|\bcookieBytes=[0-9]+\b)");
    std::istringstream input(text);
    std::string line, output;
    unsigned omitted = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 8192) { ++omitted; continue; }
        // A presence flag and handshake event names are not secret values.
        auto lower = std::regex_replace(std::regex_replace(line, token_presence, "sessionState=known"),
                                        cookie_event, "handshake_event");
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        bool sensitive = line.size() > 8192 || line.find_first_of("{}\"") != std::string::npos ||
            std::regex_search(line, email);
        for (const auto* key : {"token", "password", "authorization", "bearer", "cookie",
             "ufrag", "ice-pwd", "username", "email", "credential", "secret", "login", "api_key"})
            sensitive = sensitive || lower.find(key) != std::string::npos;
        const bool known = std::any_of(allowed.begin(), allowed.end(),
            [&](const char* prefix) { return line.find(prefix) != std::string::npos; });
        if (sensitive || !known) { ++omitted; continue; }
        line = std::regex_replace(line, endpoint, "[endpoint]");
        // Mask full NVIDIA DNS names before the JWT filter. Long ordinary DNS
        // labels can otherwise look like a three-part token.
        static const std::regex nvidia_host(
            R"(\b[a-zA-Z0-9-]+\.cloudmatchbeta\.nvidiagrid\.net\b)");
        std::vector<std::string> hosts;
        std::string protected_line;
        std::smatch match;
        auto remainder = line;
        while (std::regex_search(remainder, match, nvidia_host)) {
            protected_line += match.prefix().str() + "<route" + std::to_string(hosts.size()) + ">";
            hosts.push_back(match.str()); remainder = match.suffix().str();
        }
        line = std::regex_replace(protected_line + remainder, jwt, "[redacted]");
        for (size_t i = 0; i < hosts.size(); ++i) {
            const auto marker = "<route" + std::to_string(i) + ">";
            const auto at = line.find(marker);
            if (at != std::string::npos) line.replace(at, marker.size(), hosts[i]);
        }
        output += line + '\n';
    }
    output += "[export: " + std::to_string(omitted) + " nontechnical or sensitive lines omitted]\n";
    return output;
}

void WriteZip(const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& files) {
    if (files.size() > 32) throw std::runtime_error("Too many report files");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write the diagnostic archive");
    struct Entry { std::string name; std::uint32_t crc, size, offset; };
    std::vector<Entry> entries;
    for (const auto& [name, data] : files) {
        if (name.empty() || name.size() > 100 || name.find_first_of("/\\:") != std::string::npos ||
            name.find("..") != std::string::npos || data.size() > 2 * 1024 * 1024)
            throw std::runtime_error("Invalid report entry");
        Entry entry {name, Crc(data), static_cast<std::uint32_t>(data.size()),
                     static_cast<std::uint32_t>(out.tellp())};
        // ZIP store mode: bounded memory, no compression work on the UI thread.
        U32(out, 0x04034b50); U16(out, 20); U16(out, 0x800); U16(out, 0);
        U16(out, 0); U16(out, 33); U32(out, entry.crc);
        U32(out, entry.size); U32(out, entry.size); U16(out, name.size()); U16(out, 0);
        out << name << data;
        entries.push_back(entry);
    }
    const auto directory = static_cast<std::uint32_t>(out.tellp());
    for (const auto& entry : entries) {
        U32(out, 0x02014b50); U16(out, 20); U16(out, 20); U16(out, 0x800); U16(out, 0);
        U16(out, 0); U16(out, 33); U32(out, entry.crc);
        U32(out, entry.size); U32(out, entry.size); U16(out, entry.name.size());
        U16(out, 0); U16(out, 0); U16(out, 0); U16(out, 0); U32(out, 0); U32(out, entry.offset);
        out << entry.name;
    }
    const auto end = static_cast<std::uint32_t>(out.tellp());
    U32(out, 0x06054b50); U16(out, 0); U16(out, 0); U16(out, entries.size()); U16(out, entries.size());
    U32(out, end - directory); U32(out, directory); U16(out, 0);
    out.close();
    if (!out) throw std::runtime_error("SD card write failed while saving the report");
}

std::string Create(const std::string& home, const std::string& settings_summary,
    const std::vector<diagnostics::FileSnapshot>& captured) {
    const std::string directory = home + "/reports";
#ifdef _WIN32
    _mkdir(directory.c_str());
#else
    mkdir(directory.c_str(), 0777);
#endif
    static std::atomic<unsigned> sequence {0};
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto path = directory + "/SwitchNOW-report-" + std::to_string(stamp) + "-" +
        std::to_string(sequence.fetch_add(1)) + ".zip";
    std::vector<std::pair<std::string, std::string>> files;
    files.emplace_back("README.txt", std::string("SwitchNOW ") + kAppVersion + "\n" +
        settings_summary + "\nLogs are filtered technical excerpts, not full originals.\n"
        "Credentials, raw protocol JSON, login files, keyboard and mouse payloads are excluded.\n"
        "Some logs may belong to an earlier connection; compare timestamps.\n"
        "Low incoming FPS alone cannot distinguish server pacing from network delay.\n"
        "connection_debug.log (debug builds) records structured DNS/TCP/TLS/socket events; no raw curl headers or bodies.\n"
        "R37 INGRESS times are application observations, not NIC/kernel arrival timestamps. observedAssemblySpanMaxUs excludes any unseen first-packet time.\n"
        "Audio SUMMARY separates softEmptyResumes/softEmptyUs from full underruns and rebufferUs. A lower full-restart count alone does not prove fewer audio gaps.\n"
        "Performance CSV schema R34: last 256 windows (~3 minutes); reserved contexts: 4 critical/8 moderate/4 minor.\n"
        "attempt_id joins CSV/summary/stream_attempts.log. attempt1/attempt2 archives preserve previous transports or cloud launches independently.\n"
        "Exit streaming normally before exporting. In-memory samples cannot survive a process crash.\n"
        "FPS x100 columns: divide by 100. Times: milliseconds unless named _us.\n"
        "_new columns are per-window deltas; _epoch values are cumulative within a transport.\n"
        "Audio jitter is measured arrival variation, restart target is the next output reserve; limited blocks include gain hold/release.\n"
        "RTT source: 0 unavailable, 1 STUN, 2 SCTP. rtt_snapshot_fresh=1 means a snapshot <=500ms old.\n"
        "rtt_snapshot_age_ms distinguishes stale copies from a fresh snapshot; -1 means unknown.\n"
        "Trigger flags: 1 network evidence, 2 low incoming FPS, 4 decode gap, 8 display gap,\n"
        "16 audio fault, 32 UI interval >max(25ms,1.5 frame periods), 64 RTT spike, 128 input channel/backpressure, 256 client input/transport call >=25ms in this window, 512 late delivery with impact.\n"
        "stream_warming_up suppresses startup FPS alarms. Flags are observations, not proof of root cause. UI intervals include scheduling/vsync.\n"
        "Assessment is for the current window, not a whole-session verdict. Codes: 0 measuring, 1 stable,\n"
        "2 gaps/damage with impact, 3 incoming, 4 decoder, 5 presentation, 6 uneven delivery. Incident severity: 1 minor/2 moderate/3 critical.\n"
        "sources.csv lists read errors, missing files, truncation and byte counts instead of silently hiding them.\n"
        "server_selection.log records probes/cache/candidates/scores/requested region/assigned endpoint and stream RTT.\n"
        "TCP probes estimate a control endpoint; they are not the actual UDP media ping.\n");
    std::ostringstream sources;
    sources << "file,status,source_bytes,raw_snapshot_bytes,exported_bytes,errno,truncated\n";
    auto source = [&](const char* name) {
        const auto found = std::find_if(captured.begin(), captured.end(),
            [&](const auto& value) { return value.name == name; });
        if (found != captured.end()) return *found;
        diagnostics::FileSnapshot result;
        if (std::string(name) == "connection_debug.log")
            connection_debug::CaptureWithWriterClosed([&] { result = diagnostics::ReadTail(home, name); });
        else result = diagnostics::ReadTail(home, name);
        return result;
    };
    auto describe = [&](const auto& snapshot, std::size_t exported) {
        sources << snapshot.name << ',' << snapshot.status << ',' << snapshot.source_bytes << ','
            << snapshot.content.size() << ',' << exported << ',' << snapshot.error << ','
            << snapshot.truncated << '\n';
    };
    for (const auto* name : {"runtime.log", "proxy.log", "forwarder_debug.log", "connection_debug.log", "connection_debug.previous.log", "session_trace.log", "signaling.log", "stream_trace.log", "audio.log", "input.log", "server_selection.log", "last_session_summary.txt",
        "stream_attempts.log", "attempt1_signaling.log", "attempt1_stream_trace.log", "attempt1_audio.log", "attempt1_input.log",
        "attempt2_signaling.log", "attempt2_stream_trace.log", "attempt2_audio.log", "attempt2_input.log",
        "attempt1_server_selection.log", "attempt2_server_selection.log", "attempt1_session_trace.log", "attempt2_session_trace.log"}) {
        const auto snapshot = source(name);
        const auto content = snapshot.status == "ok" || snapshot.status == "empty"
            ? FilterLog(snapshot.content) : "[source: " + snapshot.status + " errno=" + std::to_string(snapshot.error) + "]\n";
        files.emplace_back(name, content);
        describe(snapshot, content.size());
    }
    for (const bool incident : {false, true}) {
        const auto name = incident ? "performance_incidents.csv" : "last_performance.csv";
        auto snapshot = source(name);
        auto csv = snapshot.content;
        const auto header = performance::CsvHeader(incident);
        if (!csv.starts_with(header) || csv.find_first_not_of("0123456789,-\r\n", header.size()) != std::string::npos)
        {
            csv = header; // Never export arbitrary text from a substituted CSV.
            if (snapshot.status == "ok") snapshot.status = "invalid_numeric_csv";
        }
        describe(snapshot, csv.size());
        files.emplace_back(name, std::move(csv));
    }
    files.emplace_back("sources.csv", sources.str());
    try { WriteZip(path + ".tmp", files); }
    catch (...) { std::remove((path + ".tmp").c_str()); throw; }
    if (std::rename((path + ".tmp").c_str(), path.c_str()) != 0) {
        std::remove((path + ".tmp").c_str());
        throw std::runtime_error("Cannot finish saving the diagnostic archive");
    }
    return path;
}

void SaveSessionSummary(const std::string& home, const std::string& summary) {
    std::ofstream out(home + "/last_session_summary.txt", std::ios::trunc);
    if (out) out << summary;
}
void SavePerformance(const std::string& home, const std::string& timeline, const std::string& incidents) {
    std::ofstream recent(home + "/last_performance.csv", std::ios::binary | std::ios::trunc);
    if (recent) recent << timeline;
    std::ofstream events(home + "/performance_incidents.csv", std::ios::binary | std::ios::trunc);
    if (events) events << incidents;
}
} // namespace opennow::report
