#include "gfn/session_endpoints.hpp"
#include "server_zone_policy.hpp"
#include <cassert>
#include <iostream>

using namespace opennow::gfn::endpoints;
static Selection Parse(const char* text) {
    json_error_t error{};
    auto* root=json_loads(text,0,&error);
    assert(root);
    auto result=Resolve(root);
    json_decref(root);
    return result;
}
int main() {
    assert(ControlBaseUrl(std::string("https://rig.example/")+char(0)+"suffix").empty());
    assert(ControlBaseUrl(".cloudmatchbeta.nvidiagrid.net").empty());
    assert(ControlBaseUrl("https://.cloudmatchbeta.nvidiagrid.net/").empty());
    assert(ControlBaseUrl("RIG.EXAMPLE:443/api")=="https://rig.example/api/");
    assert(ControlBaseUrl("https://[2001:db8::1]:8443/")=="https://[2001:db8::1]:8443/");
    for(const auto* bad:{"http://rig.example/","https://user@rig.example/","https://rig..example/","https://rig.example:0/","https://rig.example:65536/","https://rig.example/?secret=x","https://rig.example/#fragment","https://rig.example/\nInjected"})
        assert(ControlBaseUrl(bad).empty());
    assert(Parse(R"({"connectionInfo":[{"usage":14,"ip":".cloudmatchbeta.nvidiagrid.net"}],"sessionControlInfo":{"ip":".cloudmatchbeta.nvidiagrid.net"}})").control_host.empty());
    assert(Parse(R"({"connectionInfo":[{"usage":14,"ip":[".cloudmatchbeta.nvidiagrid.net","valid.example"]}]})").control_host=="valid.example");

    assert(SignInUrl("wss://gateway.example:8443/nvst/","peer-1","session-1",true)==
        "wss://gateway.example:8443/nvst/sign_in?peer_id=peer-1&version=3.0&peer_role=1&pairing_id=session-1");
    assert(SignInUrl("wss://gateway.example/nvst/sign_in?ticket=a%2Fb&version=0&pairing_id=old&peer_role=0&peer_id=old#fragment","p&1","s?2",false)==
        "wss://gateway.example/nvst/sign_in?ticket=a%2Fb&peer_id=p%261&version=2&peer_role=1&pairing_id=s%3F2");
    assert(SignInUrl("wss://gateway.example","p","s",true)=="wss://gateway.example/sign_in?peer_id=p&version=3.0&peer_role=1&pairing_id=s");
    bool bad_context=false;try {SignInUrl("ws://bad.example/","p","s",true);} catch(const std::exception&) {bad_context=true;}
    assert(bad_context);
    const auto rtc_root=Parse(R"({"connectionInfo":[{"usage":14,"appLevelProtocol":5,"ip":"rtc.example","port":48200}]})");
    assert(rtc_root.signaling_url=="wss://rtc.example:48200/");
    assert(Parse(R"({"connectionInfo":[{"usage":14,"appLevelProtocol":5,"ip":"rtc.example","port":70000}]})").signaling_url.empty());

    // Zone control address and media port must not replace the assigned signaling rig.
    const auto ready=Parse(R"({"serverIp":"np-waw-01.cloudmatchbeta.nvidiagrid.net",
      "sessionControlInfo":{"ip":"np-waw-01.cloudmatchbeta.nvidiagrid.net"},
      "connectionInfo":[{"usage":2,"ip":"video.example","port":48322},
        {"usage":14,"ip":["","80-250-98-36.cloudmatchbeta.nvidiagrid.net"],"port":48010,"appLevelProtocol":1,"resourcePath":"/nvst/"}]})");
    assert(ready.descriptor_summaries.size()==1);
    assert(ready.descriptor_summaries.front()=="usage=14 app_protocol=1 port=48010 resource_kind=relative");
    assert(ready.control_host=="80-250-98-36.cloudmatchbeta.nvidiagrid.net");
    assert(ready.signaling_url=="wss://80-250-98-36.cloudmatchbeta.nvidiagrid.net:443/nvst/");
    assert(ready.control_source=="usage14" && ready.signaling_source=="usage14_native_fallback");
    opennow::SessionInfo info;
    info.status=2;info.session_id="same";info.server_ip="np-waw-01.cloudmatchbeta.nvidiagrid.net";
    info.control_ip=ready.control_host;
    assert(opennow::server_zone::ShouldDirectPoll("https://prod.cloudmatchbeta.nvidiagrid.net/",
        "https://eu-poland.cloudmatchbeta.nvidiagrid.net/",info));
    assert(!opennow::server_zone::ShouldDirectPoll("https://prod.cloudmatchbeta.nvidiagrid.net/",
        "https://"+ready.control_host+"/",info));
    assert(!opennow::server_zone::ShouldDirectPoll("https://partner.example/",
        "https://eu-poland.cloudmatchbeta.nvidiagrid.net/",info));
    const auto dedicated=Parse(R"({"connectionInfo":[
      {"usage":14,"ip":"native.example","port":48010,"appLevelProtocol":1,"resourcePath":"/nvst/"},
      {"usage":16,"ip":"rtc.example","resourcePath":"wss://rtc.example:8443/nvst/"}]})");
    assert(dedicated.signaling_url=="wss://rtc.example:8443/nvst/");
    assert(dedicated.signaling_candidates.size()==2);
    assert(dedicated.control_host=="native.example");
    assert(dedicated.usage14_count==1 && dedicated.usage16_count==1);
    const auto relative=Parse(R"({"connectionInfo":[
      {"usage":14,"ip":"native.example","resourcePath":"rtsps://native.example:48010"},
      {"usage":14,"ip":"rtc.example","port":48200,"appLevelProtocol":4,"resourcePath":"/nvst/"}]})");
    assert(relative.signaling_url=="wss://rtc.example:48200/nvst/");
    assert(relative.signaling_source=="usage14_webrtc");
    assert(RecoveryEndpoint(relative.signaling_url,relative.signaling_candidates,relative.signaling_url)=="wss://native.example:443/nvst/");
    assert(RecoveryEndpoint(relative.signaling_url,relative.signaling_candidates,"wss://old.example/nvst/")==relative.signaling_url);
    assert(RecoveryEndpoint(relative.signaling_url,{relative.signaling_url},relative.signaling_url)==relative.signaling_url);
    assert(RecoveryEndpoint("wss://same.example/nvst/",{"wss://same.example:443/nvst/"},"wss://same.example/nvst/")=="wss://same.example/nvst/");
    assert(RecoveryEndpoint("wss://same.example/nvst/",{"ws://invalid.example/","wss://other.example/nvst/"},"wss://same.example/nvst/")=="wss://other.example/nvst/");
    const auto invalid_port=Parse(R"({"connectionInfo":[{"usage":14,"ip":"rtc.example","port":70000,"appLevelProtocol":4,"resourcePath":"/nvst/"}]})");
    assert(invalid_port.signaling_url.empty()); // no forged advertised port
    const auto dedup=Parse(R"({"signalingUrl":"wss://same.example/nvst/","connectionInfo":[{"usage":14,"resourcePath":"wss://same.example:443/nvst/"}]})");
    assert(dedup.signaling_candidates.size()==1);
    const auto same_route=Parse(R"({"connectionInfo":[
      {"usage":14,"ip":"same.example","port":48010,"appLevelProtocol":1,"resourcePath":"/nvst/"},
      {"usage":14,"ip":"same.example","port":443,"appLevelProtocol":4,"resourcePath":"/nvst/"}]})");
    assert(same_route.signaling_candidates.size()==1 && same_route.signaling_source=="usage14_webrtc");
    const auto moved=Parse(R"({"connectionInfo":[{"usage":14,"resourcePath":"rtsps://80.250.98.39:48010"}]})");
    assert(moved.control_host=="80.250.98.39");
    assert(moved.signaling_url=="wss://80.250.98.39:443/nvst/");
    const auto different=Parse(R"({"connectionInfo":[{"usage":14,"ip":"control.example","resourcePath":"rtsps://signaling.example:48010"}]})");
    assert(different.control_host=="control.example");
    assert(different.signaling_url=="wss://signaling.example:443/nvst/");
    const auto root_https=Parse(R"({"signalingUrl":"https://gateway.example:8443/nvst/"})");
    assert(root_https.signaling_url=="wss://gateway.example:8443/nvst/");
    const auto explicit_url=Parse(R"({"signalingUrl":"wss://gateway.example:8443/nvst/?secret=hidden",
      "connectionInfo":[{"usage":14,"ip":"rig.example"}]})");
    assert(explicit_url.signaling_source=="explicit");
    assert(explicit_url.signaling_url=="wss://gateway.example:8443/nvst/?secret=hidden");
    for(const auto& descriptor : explicit_url.descriptor_summaries) assert(descriptor.find("secret")==std::string::npos);
    assert(SafeRoute(explicit_url.signaling_url)=="gateway.example:8443/nvst/");
    assert(SafeRoute("wss://host.example/session-secret/path?token=secret")=="host.example/[custom_path]");
    assert(SafeRoute("wss://secret@host.example/nvst/")=="invalid");
    assert(ValidSignalingUrl("wss://[2001:db8::1]:443/nvst/"));
    assert(!ValidSignalingUrl("wss://[2001:db8::1]:0/nvst/"));
    const auto ipv6=Parse(R"({"connectionInfo":[{"usage":14,"resourcePath":"rtsps://[2001:db8::1]:48010"}]})");
    assert(ipv6.signaling_url=="wss://[2001:db8::1]:443/nvst/");
    const auto partner=Parse(R"({"sessionControlInfo":{"ip":["partner.example"],"port":48010,"resourcePath":"/custom/"}})");
    assert(partner.signaling_url=="wss://partner.example:443/custom/");
    const auto https=Parse(R"({"connectionInfo":[{"usage":14,"resourcePath":"https://rig.example:8443/nvst/"}]})");
    assert(https.signaling_url=="wss://rig.example:8443/nvst/");
    const auto empty=Parse(R"({"connectionInfo":[{"usage":2,"ip":"video.example","port":48322}]})");
    assert(empty.control_host.empty() && empty.signaling_url.empty());
    auto* many=json_object();auto* many_connections=json_array();
    for(int n=0;n<12;++n) {
        const auto url="wss://rig"+std::to_string(n)+".example/nvst/";
        json_array_append_new(many_connections,json_pack("{s:i,s:i,s:s}","usage",14,"port",443,"resourcePath",url.c_str()));
    }
    json_object_set_new(many,"connectionInfo",many_connections);
    const auto bounded=Resolve(many);json_decref(many);
    assert(bounded.usage14_count==12 && bounded.signaling_candidates.size()==8 && bounded.descriptor_summaries.size()==8);
    assert(Resolve(nullptr).signaling_url.empty());
    assert(Parse(R"({"connectionInfo":"invalid","sessionControlInfo":{"ip":"fallback.example"}})").control_host=="fallback.example");
    const auto duplicates=Parse(R"({"connectionInfo":[{"usage":14},{"usage":14,"ip":"assigned.example"}]})");
    assert(duplicates.control_host=="assigned.example");
    for(const auto* url : {"ws://rig.example/nvst/", "wss://", "wss://name@rig.example/", "wss://rig.example:0/",
        "wss://rig.example:65536/", "wss://rig.example:abc/", "wss://rig.example/path\nInjected", "wss://rig.example\\evil/"})
        assert(!ValidSignalingUrl(url));
    const auto invalid=Parse(R"({"signalingUrl":"wss://secret@evil/","connectionInfo":[{"usage":14,"ip":"safe.example"}]})");
    assert(invalid.signaling_url=="wss://safe.example:443/nvst/");
    std::cout<<"Session endpoint resolution tests passed.\n";
}
