#include "diagnostic_report.hpp"
#include "connection_debug.hpp"
#include "performance_recorder.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    using namespace opennow::report;
    assert(FilterLog("category=keys event=complete key=Esc\n").find("key=Esc")!=std::string::npos);
    assert(FilterLog("category=form_input event=started game=Genshin\n").find("game=Genshin")!=std::string::npos);
    assert(FilterLog("category=form_input event=started password=private-value\n").find("private-value")==std::string::npos);
    assert(FilterLog("category=gallery event=shown key=abcdef upload_us=10 elapsed_us=20\n").find("upload_us=10")!=std::string::npos);
    assert(FilterLog("category=image event=download curl_code=0 new_connections=0\n").find("new_connections=0")!=std::string::npos);
    assert(FilterLog("category=description event=resolved language=ru kind=machine reason=cache\n").find("kind=machine") != std::string::npos);
    assert(FilterLog("PROXY ts_ms=123 event=end curl_code=5\n").find("curl_code=5") != std::string::npos);
    const auto forwarder = FilterLog("2026-10-29T17:15:00Z FORWARDER first_frame_painted\n"
        "2026-10-29T17:15:01Z category=shortcut event=page_snapshot detail=saved\n");
    assert(forwarder.find("first_frame_painted") != std::string::npos);
    assert(forwarder.find("page_snapshot") != std::string::npos);
    const auto startup=FilterLog("SIGNAL_DIAG envelope rx=1 keys=peer_info,ackid name_match=1 id_valid=1\n"
        "SIGNAL_DIAG ack decision=skip_local_id_match serialized=0 write_ok=0\n"
        "SIGNAL_DIAG end origin=recovery reason=recovery_deadline offer=0 video=0\n");
    assert(startup.find("name_match=1")!=std::string::npos);
    assert(startup.find("skip_local_id_match")!=std::string::npos);
    assert(startup.find("recovery_deadline")!=std::string::npos);
    const auto filtered = FilterLog(
        "[+1ms] STREAM diag packets=123 frames=60 decodeErrors=0\n"
        "[+2ms] SESSION start preset=Balanced 1280x720@60 url=wss://host/path?ticket=private123\n"
        "[+3ms] TX {\"access_token\":\"secret123\"}\n"
        "[+4ms] LIBPEER Authorization: Bearer secret123\n"
        "[+5ms] LIBPEER iceUfrag=secret123\n"
        "[+6ms] KEY tx vk=65 sc=30\n"
        "[+7ms] SCTP cookie=secret123\n"
        "[+8ms] LIBPEER message=user@example.com\n"
        "[+9ms] LIBPEER value=abcdefghijklmnop.qrstuvwxyzabcdef.ghijklmnopqr\n"
        "[+10ms] SCTP handshake_retry chunk=1 attempt=1 result=32\n");
    assert(filtered.find("packets=123") != std::string::npos);
    const auto ingress = FilterLog("[+1ms] APP INGRESS pollGapMaxUs=1200 callbackMaxUs=34 timing=application_observation_not_kernel_arrival\n");
    assert(ingress.find("pollGapMaxUs=1200") != std::string::npos);
    const auto input_send = FilterLog("[+1ms] INPUT_SEND label=gamepad elapsedUs=85000 result=50\n");
    assert(input_send.find("label=gamepad elapsedUs=85000") != std::string::npos);
    assert(filtered.find("1280x720@60") != std::string::npos);
    assert(filtered.find("handshake_retry") != std::string::npos);
    assert(filtered.find("secret123") == std::string::npos);
    assert(filtered.find("private123") == std::string::npos);
    assert(filtered.find("user@example.com") == std::string::npos);
    assert(filtered.find("KEY tx") == std::string::npos);
    assert(filtered.find("abcdefghijklmnop") == std::string::npos);
    assert(filtered.find("wss://") == std::string::npos);
    const auto presence = FilterLog("POLL parsed: status=2 sessionToken=present\n"
        "POLL parsed: status=1 sessionToken=missing\n"
        "POLL parsed: sessionToken=present_extra\n"
        "POLL parsed: sessionToken=private123\n"
        "LIBPEER internal_cookie_echo prepared cookieBytes=45\n"
        "REGION candidate name=Frankfurt scoreMs=38\n");
    assert(presence.find("sessionToken=present\n") != std::string::npos);
    assert(presence.find("sessionToken=missing\n") != std::string::npos);
    assert(presence.find("private123") == std::string::npos && presence.find("present_extra") == std::string::npos);
    assert(presence.find("cookieBytes=45") != std::string::npos);
    assert(presence.find("scoreMs=38") != std::string::npos);
    const auto queue = FilterLog("AD update accepted: status=1 queuePosition=126 queueSource=seat_setup queueConflict=1 seatSetupStep=1 sessionToken=missing\n");
    assert(queue.find("queuePosition=126 queueSource=seat_setup queueConflict=1 seatSetupStep=1") != std::string::npos);
    const auto routes = FilterLog("ROUTING host=eu-netherlands-1.cloudmatchbeta.nvidiagrid.net\n"
        "ADAPT mode=Balanced reason=recovering waitUs=1000\n"
        "PAUSE complete cause=transport durationMs=113\n"
        "LIBPEER value=abcdefghijklmnop.qrstuvwxyzabcdef.ghijklmnopqr\n");
    assert(routes.find("eu-netherlands-1.cloudmatchbeta.nvidiagrid.net") != std::string::npos);
    assert(routes.find("reason=recovering") != std::string::npos);
    assert(routes.find("durationMs=113") != std::string::npos);
    assert(routes.find("abcdefghijklmnop") == std::string::npos);
    const auto attempt = FilterLog("ATTEMPT start id=123 cloud_session=session-a\n"
        "SIGNAL envelope parse_ok=1 peer_info=1 peer_msg=0 heartbeat=0\n"
        "SIGNAL payload kind=offer has_sdp=1\n");
    assert(attempt.find("id=123") != std::string::npos);
    assert(attempt.find("kind=offer") != std::string::npos);
    const std::string root = "build/host-tests/report-fixture";
    std::filesystem::create_directories(root);
    std::ofstream(root + "/runtime.log") << "category=app event=start version=R27\n";
    std::ofstream(root + "/auth_accounts.json") << "secret123";
    std::ofstream(root + "/connection_debug.log") << "NETDBG request=1 event=result phase=socket_create os_errno=24 errno_name=EMFILE\n";
    std::ofstream(root + "/attempt1_stream_trace.log") << "ATTEMPT end id=122 phase=waiting_offer\n";
    std::ofstream(root + "/stream_attempts.log") << "ATTEMPT start id=123 cloud_session=session-a\n";
    SaveSessionSummary(root, "session_metrics decoded=100 displayed=99\n");
    SavePerformance(root, opennow::performance::CsvHeader(false) + "secret123\n",
        opennow::performance::CsvHeader(true));
#if defined(SWITCHNOW_NETWORK_DEBUG) && SWITCHNOW_NETWORK_DEBUG
    opennow::connection_debug::Initialize(root,"R37 test");
    opennow::connection_debug::Event(1,"result",{{"phase","socket_create"},{"errno_name","EMFILE"}});
#endif
    const auto archive = Create(root, "Stream: 1280x720 @ 60 fps");
    std::ifstream in(archive, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), {});
    assert(bytes.starts_with("PK\x03\x04"));
    assert(bytes.find("session_metrics decoded=100") != std::string::npos);
    assert(bytes.find("auth_accounts.json") == std::string::npos);
    assert(bytes.find("secret123") == std::string::npos);
    assert(bytes.find("last_performance.csv") != std::string::npos);
    assert(bytes.find("id=122 phase=waiting_offer") != std::string::npos);
    assert(bytes.find("id=123 cloud_session=session-a") != std::string::npos);
    assert(bytes.find("sources.csv") != std::string::npos);
    assert(bytes.find("connection_debug.log") != std::string::npos);
    assert(bytes.find("phase=socket_create os_errno=24 errno_name=EMFILE") != std::string::npos);
    assert(bytes.find("signaling.log,missing,") != std::string::npos);
    opennow::performance::Recorder recording;
    opennow::performance::Row row {};
    row[opennow::performance::TimeMs] = 12345;
    recording.Record(row);
    SavePerformance(root, recording.TimelineCsv(), recording.IncidentsCsv());
    const auto valid_archive = Create(root, "Configured stream: 1280x720");
    std::ifstream valid(valid_archive, std::ios::binary);
    const std::string valid_bytes((std::istreambuf_iterator<char>(valid)), {});
    assert(valid_bytes.find("\n12345,") != std::string::npos);
    opennow::diagnostics::FileSnapshot captured;
    captured.name = "signaling.log"; captured.status = "ok";
    captured.content = "STREAM diag packets=777 frames=60\n";
    captured.source_bytes = captured.content.size();
    const auto snapshot_archive = Create(root, "Captured writer snapshot", {captured});
    std::ifstream snap(snapshot_archive, std::ios::binary);
    const std::string snapshot_bytes((std::istreambuf_iterator<char>(snap)), {});
    assert(snapshot_bytes.find("packets=777") != std::string::npos);
    assert(snapshot_bytes.find("signaling.log,ok,") != std::string::npos);
    bool rejected = false;
    try { WriteZip(root + "/invalid.zip", {{"../unsafe", "data"}}); }
    catch (...) { rejected = true; }
    assert(rejected);
#if defined(SWITCHNOW_NETWORK_DEBUG) && SWITCHNOW_NETWORK_DEBUG
    assert(bytes.find("connection_debug.log,ok,") != std::string::npos);
    opennow::connection_debug::Event(0,"after_report_capture");
    std::ifstream resumed(root+"/connection_debug.log");
    const std::string resumed_bytes((std::istreambuf_iterator<char>(resumed)),{});
    assert(resumed_bytes.find("event=after_report_capture") != std::string::npos);
    resumed.close();
    opennow::connection_debug::Shutdown();
#endif
}
