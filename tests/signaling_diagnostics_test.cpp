#include "signaling_diagnostics.hpp"
#include <jansson.h>
#include <cassert>
#include <string>
using namespace opennow::signaling;
static json_t* Parse(const char* text) { json_error_t e{};return json_loads(text,JSON_DECODE_ANY,&e); }
int main() {
    Diagnostics d;
    auto summary=[&] { return d.Summary(true,false,0,false,EndReason::None,""); };
    assert(summary().find("phase=registration_not_sent")!=std::string::npos);
    assert(d.Registration(17,false,100).find("write_ok=0")!=std::string::npos);
    assert(summary().find("phase=registration_write_failed")!=std::string::npos);
    d.Registration(18,true,100);
    assert(summary().find("phase=awaiting_peer_assignment")!=std::string::npos);
    auto* root=Parse(R"({"peer_info":{"name":"expected","id":5,"connected":true,"peerRole":0},"ackid":7})");
    auto line=d.Observe(root,"expected");json_decref(root);
    assert(line.find("name_match=1 id_valid=1")!=std::string::npos);
    assert(line.find("peer_connected=1 peer_role=0")!=std::string::npos);
    assert(summary().find("phase=awaiting_offer")!=std::string::npos);
    assert(d.Ack(true,false,false).find("skip_local_id_match")!=std::string::npos);
    root=Parse(R"({"ack":18})");line=d.Observe(root,"expected");json_decref(root);
    assert(line.find("registration_ack_match=1")!=std::string::npos);
    assert(summary().find("registrationAckSeen=1")!=std::string::npos);
    root=Parse(R"({"hb":1})");
    for(unsigned i=0;i<30;++i) d.Observe(root,"expected");
    json_decref(root);
    assert(summary().find("phase=awaiting_offer")!=std::string::npos);
    assert(summary().find("offer=0 remoteIce=0 video=0")!=std::string::npos);
    assert(d.Ack(false,true,false).find("write_ok=0")!=std::string::npos);
    assert(d.Ack(false,true,true).find("write_ok=1")!=std::string::npos);
    assert(summary().find("ackTx=2 ackWritten=1 localIdAckSkipped=1")!=std::string::npos);
    assert(d.Summary(true,true,0,false,EndReason::None,"").find("phase=offer_received")!=std::string::npos);
    assert(d.Summary(true,true,2,false,EndReason::None,"").find("phase=ice_candidates")!=std::string::npos);
    assert(d.Summary(true,true,2,true,EndReason::StopRequested,"").find("phase=video")!=std::string::npos);
    assert(d.Summary(true,false,0,false,EndReason::RecoveryDeadline,"").find("end=recovery_deadline")!=std::string::npos);
    assert(std::string(EndReasonName(EndReason::InitialOfferTimeout))=="initial_offer_timeout");
    assert(std::string(EndReasonName(EndReason::InitialIceTimeout))=="initial_ice_timeout");

    Diagnostics privacy;
    root=Parse(R"({"peer_info":{"name":"private_name","id":"private_id","connected":false},"secret_dynamic_key":"private_value","error":{"code":403,"message":"private_error"},"type":"private_type","message":"private_text","status":6})");
    line=privacy.Observe(root,"expected");json_decref(root);
    assert(line.find("name_match=0 id_valid=0")!=std::string::npos);
    assert(line.find("other_keys=1")!=std::string::npos);
    assert(line.find("status=6")!=std::string::npos && line.find("error_code=403")!=std::string::npos);
    for(const char* secret:{"private_name","private_id","private_value","private_error","private_type","private_text","secret_dynamic_key"})
        assert(line.find(secret)==std::string::npos);
    root=Parse(R"({"peer_msg":{"msg":{"type":"offer","sdp":"private_sdp"}}})");
    line=privacy.Observe(root,"expected");json_decref(root);
    assert(line.find("msg_kind=object")!=std::string::npos && line.find("private_sdp")==std::string::npos);
    root=Parse(R"({"type":"offer","sdp":"private_sdp","error":"private_error"})");
    line=DescribePayload(root,true,500);json_decref(root);
    assert(line.find("kind=offer sdp_kind=string")!=std::string::npos);
    assert(line.find("private_")==std::string::npos);
    assert(DescribePayload(nullptr,false,20).find("parsed=0")!=std::string::npos);
    root=Parse("[]");line=privacy.Observe(root,"expected");json_decref(root);
    assert(line.find("root=array")!=std::string::npos);
    assert(privacy.Observe(nullptr,"expected").find("parse_ok=0")!=std::string::npos);
    assert(std::string(TransportErrorKind("Receive error: private_host"))=="receive_error");
    assert(std::string(TransportErrorKind("private_error"))=="other");
    assert(std::string(TransportErrorKind("Send timeout"))=="send_timeout");

    Diagnostics bounded;
    root=Parse(R"({"ack":1234})");
    unsigned lines=0;
    for(unsigned i=0;i<1000;++i) if(!bounded.Observe(root,"expected").empty()) ++lines;
    json_decref(root);assert(lines==64);
    lines=0;for(unsigned i=0;i<1000;++i) if(!bounded.Ack(false,true,true).empty()) ++lines;
    assert(lines==64);
}
