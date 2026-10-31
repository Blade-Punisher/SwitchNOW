#include "connection_debug.hpp"
#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <vector>
#include <iterator>
int main() {
    using namespace opennow::connection_debug;
    const auto target=Describe("https://user:secret@prod.cloudmatchbeta.nvidiagrid.net/v2/session/private-session?token=hidden");
    assert(target.host=="prod.cloudmatchbeta.nvidiagrid.net" && target.route=="session_by_id" && target.secure);
    assert(Describe("https://prod.example/v2/serverInfo?key=private").route=="serverInfo");
    assert(Describe("https://host.example/account/private-user/email").route=="other");
    assert(Describe("invalid").host=="invalid");
    ResultInfo result;result.curl_code=6;
    assert(std::string(Phase(result,true,false))=="dns");
    result.curl_code=7;result.socket_attempts=result.socket_failures=1;
    assert(std::string(Phase(result,true,false))=="socket_create");
    result.socket_failures=0;
    assert(std::string(Phase(result,true,false))=="tcp_connect");
    result.curl_code=60;
    assert(std::string(Phase(result,true,false))=="tls");
    result.curl_code=28;result.connect_us=1000;
    assert(std::string(Phase(result,true,false))=="tls_or_before_http");
    result.curl_code=0;result.http_status=401;
    assert(std::string(Phase(result,true,false))=="http_response");
    result.http_status=0;
    assert(std::string(Phase(result,false,true))=="tcp_connected");
    assert(!MessageClass("Authorization: Bearer secret123"));
    assert(std::string(MessageClass("Trying host?token=secret123"))=="tcp_attempt");
    assert(std::string(ErrnoName(EMFILE))=="EMFILE");
    const std::string root="build/host-tests/connection-debug-fixture";
    std::filesystem::create_directories(root);
    errno=123;Initialize(root,"1.2.1 R35 debug");assert(errno==123);
    assert(Enabled());
    {
        Request request("GET","https://user:secret@prod.cloudmatchbeta.nvidiagrid.net/v2/session/private-session?token=hidden",false);
        request.Text("Trying https://host/?token=secret123");
        request.Text("Authorization: Bearer secret123");
        request.Socket(-1,2,1,6,"1.2.3.4",443,EMFILE,1234);
        result={};result.curl_code=7;result.os_error=EMFILE;
        request.Finish(result);
        assert(errno==123);
    }
    {
        Request request("GET","https://host.example/v2/serverInfo",false);
        request.Socket(42,2,1,6,"1.2.3.4",443,0,0);
        request.Closed(42,0,0,0);
        result={};result.http_status=200;request.Finish(result);
    }
    // The persistent log handle remains writable if no new stdio descriptor
    // can be opened (the very condition this build is intended to diagnose).
    std::vector<std::FILE*> held;
    bool exhausted=false;
    for (unsigned i=0;i<2048;++i) {
        auto* file=std::fopen((root+"/descriptor-fixture.bin").c_str(),"ab");
        if (!file) { exhausted=true;break; }
        held.push_back(file);
    }
    Event(0,"descriptor_pressure",{{"new_open_failed",exhausted?"1":"0"}});
    for (auto* file : held) std::fclose(file);
    CaptureWithWriterClosed([&] {
        std::ifstream snapshot(root+"/connection_debug.log");
        const std::string bytes((std::istreambuf_iterator<char>(snapshot)),{});
        assert(bytes.find("event=descriptor_pressure") != std::string::npos);
    });
    bool threw = false;
    try { CaptureWithWriterClosed([] { throw 42; }); } catch (int) { threw = true; }
    assert(threw);
    Event(0,"after_snapshot");
    SystemSnapshot("test",true);
    std::ifstream in(root+"/connection_debug.log");
    const std::string text((std::istreambuf_iterator<char>(in)),{});
    assert(text.find("phase=socket_create")!=std::string::npos);
    assert(text.find("errno_name=EMFILE")!=std::string::npos);
    assert(text.find("curl_sockets_tracked=0")!=std::string::npos);
    assert(text.find("secret")==std::string::npos && text.find("hidden")==std::string::npos);
    assert(text.find("private-session")==std::string::npos && text.find("Authorization")==std::string::npos);
    assert(text.find("route=session_by_id")!=std::string::npos);
    assert(text.find("event=descriptor_pressure")!=std::string::npos);
    assert(text.find("event=after_snapshot")!=std::string::npos);
    in.close();
    Initialize(root,"next_boot");
    assert(std::filesystem::exists(root+"/connection_debug.previous.log"));
    Shutdown();assert(!Enabled());
}
