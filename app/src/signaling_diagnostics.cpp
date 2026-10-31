#include "signaling_diagnostics.hpp"
#include <jansson.h>
#include <array>
#include <climits>
#include <cstring>

namespace opennow::signaling {
namespace {
const char* Kind(json_t* v) {
    if(!v) return "missing";
    if(json_is_object(v)) return "object";
    if(json_is_array(v)) return "array";
    if(json_is_integer(v)) return "integer";
    if(json_is_string(v)) return "string";
    if(json_is_boolean(v)) return "boolean";
    if(json_is_null(v)) return "null";
    return "other";
}
const char* ValueName(json_t* v) {
    const char* s=json_string_value(v);
    if(!s) return Kind(v);
    for(const char* allowed:{"offer","answer","candidate","error","ready","ok","failed",
                           "connected","disconnected","pending","ack","peer_info","peer_msg"})
        if(std::strcmp(s,allowed)==0) return allowed;
    return "other";
}
std::string Code(json_t* value) {
    if(json_is_object(value)) value=json_object_get(value,"code");
    if(json_is_integer(value)) {
        const auto n=json_integer_value(value);
        if(n>=-999999 && n<=999999) return std::to_string(n);
        return "out_of_range";
    }
    return Kind(value);
}
const char* Bit(bool b) { return b ? "1" : "0"; }
}
const char* EndReasonName(EndReason r) {
    switch(r) {
        case EndReason::InitialOfferTimeout:return "initial_offer_timeout";
        case EndReason::InitialIceTimeout:return "initial_ice_timeout";
        case EndReason::RecoveryDeadline:return "recovery_deadline";
        case EndReason::RecoveryPeerTerminal:return "recovery_peer_terminal";
        case EndReason::StopRequested:return "stop_requested";
        default:return "none";
    }
}
const char* TransportErrorKind(const std::string& e) {
    if(e.empty()) return "none";
    if(e.starts_with("Send timeout")) return "send_timeout";
    if(e.starts_with("Send stalled")) return "send_stalled";
    if(e.starts_with("Send error")) return "send_error";
    if(e.starts_with("Receive error")) return "receive_error";
    if(e.starts_with("Remote endpoint closed")) return "remote_closed";
    if(e.starts_with("Incoming WebSocket")) return "invalid_frame";
    return "other";
}
std::string DescribePayload(json_t* p,bool parsed,std::size_t bytes) {
    return "SIGNAL_DIAG payload parsed="+std::string(Bit(parsed))+" root="+Kind(p)+
        " bytes="+std::to_string(bytes)+" kind="+ValueName(json_object_get(p,"type"))+
        " sdp_kind="+Kind(json_object_get(p,"sdp"))+
        " error_kind="+Kind(json_object_get(p,"error"))+
        " error_code="+Code(json_object_get(p,"error"));
}
std::string Diagnostics::Registration(std::int64_t id,bool written,std::size_t bytes) {
    ++registration_tx_; if(written) ++registration_written_;
    registration_id_=id;have_registration_=true;last_registration_written_=written;
    return "SIGNAL_DIAG registration tx="+std::to_string(registration_tx_)+
        " write_ok="+Bit(written)+" bytes="+std::to_string(bytes);
}
std::string Diagnostics::Ack(bool skipped,bool serialized,bool written) {
    if(skipped) ++local_id_skipped_;
    else if(serialized) {++ack_tx_;if(written) ++ack_written_;}
    if(++ack_decisions_>64) return "";
    return "SIGNAL_DIAG ack decision="+std::string(skipped ? "skip_local_id_match" : "send")+
        " serialized="+Bit(serialized)+" write_ok="+Bit(written);
}
std::string Diagnostics::Observe(json_t* root,const std::string& expected) {
    ++rx_;
    if(!json_is_object(root)) {
        ++malformed_;
        return rx_<=64 ? "SIGNAL_DIAG envelope root="+std::string(Kind(root))+" parse_ok="+Bit(root!=nullptr) : "";
    }
    constexpr std::array keys{"peer_info","peer_msg","hb","ackid","ack","error","status","type",
        "code","message","sdp","offer","data","payload","peerInfo","peerMessage","errorCode"};
    std::string present;
    unsigned known=0;
    for(auto key:keys) if(json_object_get(root,key)) {
        if(known++) present+=',';
        present+=key;
    }
    const auto others=json_object_size(root)-known;
    auto* pi=json_object_get(root,"peer_info");
    auto* id=json_object_get(pi,"id");
    auto* name=json_object_get(pi,"name");
    const bool name_match=json_is_string(name) && expected==json_string_value(name);
    const bool id_ok=json_is_integer(id) && json_integer_value(id)>=0 && json_integer_value(id)<=INT_MAX;
    if(pi) ++peer_info_;
    if(name_match && id_ok) {identified_=true;++identities_;}
    auto* ack=json_object_get(root,"ack");
    const bool reg_ack=have_registration_ && json_is_integer(ack) && json_integer_value(ack)==registration_id_;
    if(reg_ack) ++registration_ack_;
    auto* pm=json_object_get(root,"peer_msg");
    auto* error=json_object_get(root,"error");
    const bool unknown=!pi && !pm && !json_object_get(root,"hb") && !ack && !json_object_get(root,"ackid");
    if(unknown) ++unknown_;
    if(error) ++errors_;
    // Bound detailed output, including after the handshake. No arbitrary JSON
    // field names or string values are copied into these records.
    if(rx_>64 && !(error && errors_<=8) && !(pi && peer_info_<=8)) return "";
    return "SIGNAL_DIAG envelope rx="+std::to_string(rx_)+" keys="+(present.empty()?"none":present)+
        " other_keys="+std::to_string(others)+" unknown="+Bit(unknown)+
        " peer_info_kind="+Kind(pi)+" name_match="+Bit(name_match)+" id_valid="+Bit(id_ok)+
        " peer_connected_kind="+Kind(json_object_get(pi,"connected"))+
        " peer_connected="+(json_is_boolean(json_object_get(pi,"connected")) ? Bit(json_is_true(json_object_get(pi,"connected"))) : "unknown")+
        " peer_role="+Code(json_object_get(pi,"peerRole"))+
        " ackid_kind="+Kind(json_object_get(root,"ackid"))+" ack_kind="+Kind(ack)+
        " registration_ack_match="+Bit(reg_ack)+
        " peer_msg_kind="+Kind(pm)+" msg_kind="+Kind(json_object_get(pm,"msg"))+
        " top_type="+ValueName(json_object_get(root,"type"))+
        " status="+(json_is_integer(json_object_get(root,"status")) ? Code(json_object_get(root,"status")) : ValueName(json_object_get(root,"status")))+
        " error_kind="+Kind(error)+" error_code="+Code(error ? error :
            json_object_get(root,"code") ? json_object_get(root,"code") : json_object_get(root,"errorCode"));
}
std::string Diagnostics::Summary(bool socket,bool offer,unsigned ice,bool video,EndReason end,
                                 const std::string& transport_error) const {
    const char* phase=video ? "video" : offer ? (ice ? "ice_candidates" : "offer_received") :
        !socket ? "socket_not_connected" : !have_registration_ ? "registration_not_sent" :
        !last_registration_written_ ? "registration_write_failed" :
        !identified_ ? "awaiting_peer_assignment" : "awaiting_offer";
    return "SIGNAL_DIAG summary phase="+std::string(phase)+" end="+EndReasonName(end)+
        " socket="+Bit(socket)+" rx="+std::to_string(rx_)+" peerInfo="+std::to_string(peer_info_)+
        " identityObservations="+std::to_string(identities_)+" unknown="+std::to_string(unknown_)+
        " malformed="+std::to_string(malformed_)+" errors="+std::to_string(errors_)+
        " registrationTx="+std::to_string(registration_tx_)+" registrationWritten="+std::to_string(registration_written_)+
        " registrationAckSeen="+std::to_string(registration_ack_)+" ackTx="+std::to_string(ack_tx_)+
        " ackWritten="+std::to_string(ack_written_)+" localIdAckSkipped="+std::to_string(local_id_skipped_)+
        " offer="+Bit(offer)+" remoteIce="+std::to_string(ice)+" video="+Bit(video)+
        " transportError="+TransportErrorKind(transport_error);
}
}
