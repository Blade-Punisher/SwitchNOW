#include "webrtc_session.hpp"
#include "nvst_sdp.hpp"
#include "stream/RemoteCandidatePolicy.hpp"
#include "internal.hpp"

#include <jansson.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace opennow::webrtc::internal;


namespace
{

std::string ReplaceAll(std::string value, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

std::vector<std::string> SplitSdpLines(const std::string& sdp)
{
    std::vector<std::string> lines;
    size_t start = 0;

    while (start < sdp.size()) {
        size_t end = sdp.find("\r\n", start);
        if (end == std::string::npos)
            end = sdp.find('\n', start);

        if (end == std::string::npos) {
            lines.push_back(sdp.substr(start));
            break;
        }

        lines.push_back(sdp.substr(start, end - start));
        start = end + (sdp[end] == '\r' && end + 1 < sdp.size() && sdp[end + 1] == '\n' ? 2 : 1);
    }

    return lines;
}

std::string JoinSdpLines(const std::vector<std::string>& lines)
{
    std::string result;
    for (const auto& line : lines) {
        result += line;
        result += "\r\n";
    }
    return result;
}

bool StartsWithString(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}


int ParseIntegerAttribute(const std::string& sdp, const std::string& attribute, int fallback)
{
    const std::string prefix = "a=" + attribute + ":";
    for (const auto& line : SplitSdpLines(sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const std::string raw = line.substr(prefix.size());
        char* end = nullptr;
        long value = 0;
        if (raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0)
            value = std::strtol(raw.c_str() + 2, &end, 16);
        else
            value = std::strtol(raw.c_str(), &end, 10);

        if (end != raw.c_str())
            return static_cast<int>(value);
    }
    return fallback;
}

opennow::webrtc::RiInputCapabilities ParseRiInputCapabilities(const std::string& offer_sdp)
{
    opennow::webrtc::RiInputCapabilities caps;
    const int threshold = ParseIntegerAttribute(offer_sdp, "ri.partialReliableThresholdMs", caps.partial_reliable_threshold_ms);
    if (threshold > 0)
        caps.partial_reliable_threshold_ms = std::max(1, std::min(5000, threshold));
    caps.hid_device_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.hidDeviceMask", static_cast<int>(caps.hid_device_mask)));
    caps.partial_reliable_gamepad_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.enablePartiallyReliableTransferGamepad", static_cast<int>(caps.partial_reliable_gamepad_mask)));
    caps.partial_reliable_hid_mask = static_cast<uint32_t>(ParseIntegerAttribute(
        offer_sdp, "ri.enablePartiallyReliableTransferHid", static_cast<int>(caps.partial_reliable_hid_mask)));
    return caps;
}

int ExtractRtpmapPayloadType(const std::string& line, const char* codec)
{
    constexpr const char* prefix = "a=rtpmap:";
    if (!StartsWith(line, prefix))
        return 0;

    const size_t pt_start = std::strlen(prefix);
    const size_t space = line.find(' ', pt_start);
    if (space == std::string::npos)
        return 0;

    const std::string codec_prefix = std::string(codec) + "/";
    const std::string codec_value = line.substr(space + 1);
    if (!StartsWithString(codec_value, codec_prefix))
        return 0;

    char* end = nullptr;
    long pt = std::strtol(line.c_str() + pt_start, &end, 10);
    if (pt <= 0 || pt > 127)
        return 0;

    return static_cast<int>(pt);
}

std::string FindFmtpForPayload(const std::vector<std::string>& lines, int payload_type)
{
    const std::string prefix = "a=fmtp:" + std::to_string(payload_type);
    for (const auto& line : lines) {
        if (StartsWithString(line, prefix))
            return line;
    }
    return "";
}

std::vector<std::string> FindRtcpFbForPayload(const std::vector<std::string>& lines, int payload_type)
{
    std::vector<std::string> feedback;
    const std::string prefix = "a=rtcp-fb:" + std::to_string(payload_type) + " ";
    for (const auto& line : lines) {
        if (line == prefix + "nack" || line == prefix + "nack pli")
            feedback.push_back(line);
    }
    return feedback;
}

int SelectOfferH264PayloadType(const std::string& offer_sdp)
{
    const std::vector<std::string> lines = SplitSdpLines(offer_sdp);
    std::vector<int> h264_payloads;
    bool in_video = false;

    for (const auto& line : lines) {
        if (StartsWith(line, "m="))
            in_video = StartsWith(line, "m=video");

        if (!in_video)
            continue;

        int pt = ExtractRtpmapPayloadType(line, "H264");
        if (pt > 0)
            h264_payloads.push_back(pt);
    }

    for (int pt : h264_payloads) {
        const std::string fmtp = FindFmtpForPayload(lines, pt);
        if (fmtp.find("packetization-mode=1") != std::string::npos)
            return pt;
    }

    return h264_payloads.empty() ? 96 : h264_payloads.front();
}

int ExtractOfferMediaPort(const std::string& offer_sdp, const char* media_name)
{
    const std::string prefix = std::string("m=") + media_name + " ";
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const size_t port_start = prefix.size();
        const size_t port_end = line.find(' ', port_start);
        const std::string port_text = line.substr(
            port_start,
            port_end == std::string::npos ? std::string::npos : port_end - port_start);

        char* end = nullptr;
        const long port = std::strtol(port_text.c_str(), &end, 10);
        if (end != port_text.c_str() && port > 0 && port <= 65535)
            return static_cast<int>(port);
    }

    return 0;
}

int CountSdpLinesWithPrefix(const std::string& sdp, const std::string& prefix)
{
    int count = 0;
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            ++count;
    }
    return count;
}


std::string CompactSignalingMessage(const std::string& msg)
{
    json_error_t error;
    json_t* root = json_loads(msg.c_str(), 0, &error);
    if (!root)
        return "RX invalid-json " + PreviewText(msg, 120);

    std::string summary = "RX message";
    json_t* ack = json_object_get(root, "ack");
    if (ack && json_is_integer(ack)) {
        summary = "RX ack=" + std::to_string(json_integer_value(ack));
    } else if (json_object_get(root, "hb")) {
        summary = "RX hb";
    } else if (json_t* peer_info = json_object_get(root, "peer_info"); peer_info && json_is_object(peer_info)) {
        const char* name = json_string_value(json_object_get(peer_info, "name"));
        json_t* id = json_object_get(peer_info, "id");
        summary = "RX peer_info";
        if (name)
            summary += " name=" + std::string(name);
        if (id && json_is_integer(id))
            summary += " id=" + std::to_string(json_integer_value(id));
    } else if (json_t* peer_msg = json_object_get(root, "peer_msg"); peer_msg && json_is_object(peer_msg)) {
        const char* raw_payload = json_string_value(json_object_get(peer_msg, "msg"));
        if (raw_payload) {
            json_error_t payload_error;
            json_t* payload = json_loads(raw_payload, 0, &payload_error);
            if (payload) {
                const char* type = json_string_value(json_object_get(payload, "type"));
                const char* candidate = json_string_value(json_object_get(payload, "candidate"));
                if (type && std::string(type) == "offer") {
                    const char* sdp = json_string_value(json_object_get(payload, "sdp"));
                    const char* nvst = json_string_value(json_object_get(payload, "nvstSdp"));
                    summary = "RX offer sdpBytes=" + std::to_string(sdp ? std::strlen(sdp) : 0) +
                        " nvstBytes=" + std::to_string(nvst ? std::strlen(nvst) : 0);
                } else if (candidate) {
                    summary = "RX candidate " + PreviewText(candidate, 120);
                } else {
                    summary = "RX peer_msg " + PreviewText(raw_payload, 120);
                }
                json_decref(payload);
            } else {
                summary = "RX peer_msg raw " + PreviewText(raw_payload, 120);
            }
        }
    }

    json_decref(root);
    return summary;
}

std::string MediaKindFromMLine(const std::string& line)
{
    if (StartsWithString(line, "m=audio "))
        return "audio";
    if (StartsWithString(line, "m=video "))
        return "video";
    if (StartsWithString(line, "m=application "))
        return "application";
    return "";
}

std::vector<std::string> ExtractOfferMediaOrder(const std::string& offer_sdp)
{
    std::vector<std::string> order;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        const std::string media = MediaKindFromMLine(line);
        if (!media.empty())
            order.push_back(media);
    }
    return order;
}

std::string ExtractOfferMid(const std::string& offer_sdp, const std::string& media)
{
    bool in_media = false;
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "m="))
            in_media = MediaKindFromMLine(line) == media;
        if (in_media && StartsWithString(line, "a=mid:"))
            return line.substr(std::strlen("a=mid:"));
    }
    return "";
}

std::string ExtractOfferBundleGroup(const std::string& offer_sdp)
{
    for (const auto& line : SplitSdpLines(offer_sdp)) {
        if (StartsWithString(line, "a=group:BUNDLE "))
            return line;
    }
    return "";
}

struct SdpMediaSection {
    std::string media;
    std::vector<std::string> lines;
};

std::string AlignAnswerSdpToOffer(const std::string& answer_sdp, const std::string& offer_sdp)
{
    std::vector<std::string> session_lines;
    std::vector<SdpMediaSection> sections;
    std::vector<std::string> candidates;
    std::set<std::string> seen_candidates;

    for (const auto& line : SplitSdpLines(answer_sdp)) {
        if (StartsWithString(line, "m=")) {
            sections.push_back({MediaKindFromMLine(line), {line}});
            continue;
        }

        if (StartsWithString(line, "a=candidate:")) {
            if (seen_candidates.insert(line).second)
                candidates.push_back(line);
            continue;
        }

        if (sections.empty())
            session_lines.push_back(line);
        else
            sections.back().lines.push_back(line);
    }

    const std::string offer_bundle = ExtractOfferBundleGroup(offer_sdp);
    bool bundle_replaced = false;
    for (auto& line : session_lines) {
        if (!offer_bundle.empty() && StartsWithString(line, "a=group:BUNDLE ")) {
            line = offer_bundle;
            bundle_replaced = true;
        }
    }
    if (!offer_bundle.empty() && !bundle_replaced)
        session_lines.push_back(offer_bundle);

    for (auto& section : sections) {
        const std::string offer_mid = ExtractOfferMid(offer_sdp, section.media);
        if (offer_mid.empty())
            continue;

        bool mid_replaced = false;
        for (auto& line : section.lines) {
            if (StartsWithString(line, "a=mid:")) {
                line = "a=mid:" + offer_mid;
                mid_replaced = true;
                break;
            }
        }
        if (!mid_replaced && section.lines.size() > 1)
            section.lines.insert(section.lines.begin() + 1, "a=mid:" + offer_mid);
    }

    std::vector<SdpMediaSection> ordered;
    std::vector<bool> used(sections.size(), false);
    for (const auto& media : ExtractOfferMediaOrder(offer_sdp)) {
        for (size_t i = 0; i < sections.size(); ++i) {
            if (!used[i] && sections[i].media == media) {
                ordered.push_back(sections[i]);
                used[i] = true;
                break;
            }
        }
    }
    for (size_t i = 0; i < sections.size(); ++i) {
        if (!used[i])
            ordered.push_back(sections[i]);
    }

    if (!ordered.empty() && !candidates.empty()) {
        auto& first_section = ordered.front().lines;
        first_section.insert(first_section.end(), candidates.begin(), candidates.end());
    }

    std::vector<std::string> out = session_lines;
    for (const auto& section : ordered)
        out.insert(out.end(), section.lines.begin(), section.lines.end());
    return JoinSdpLines(out);
}

std::string AdaptAnswerSdpToOffer(
    const std::string& answer_sdp,
    const std::string& offer_sdp,
    const opennow::StreamSettings& settings)
{
    const int h264_payload_type = SelectOfferH264PayloadType(offer_sdp);
    const std::vector<std::string> offer_lines = SplitSdpLines(offer_sdp);
    const std::string offer_h264_fmtp = FindFmtpForPayload(offer_lines, h264_payload_type);
    const std::vector<std::string> offer_h264_feedback = FindRtcpFbForPayload(offer_lines, h264_payload_type);
    std::string offer_audio_red_rtpmap;
    std::string offer_audio_red_fmtp;
    bool offer_in_audio = false;
    for (const auto& line : offer_lines) {
        if (StartsWith(line, "m=")) {
            offer_in_audio = StartsWith(line, "m=audio");
            continue;
        }
        if (!offer_in_audio)
            continue;
        if (StartsWithString(line, "a=rtpmap:63 ") &&
            (line.find("red/48000") != std::string::npos ||
             line.find("RED/48000") != std::string::npos)) {
            offer_audio_red_rtpmap = line;
        } else if (StartsWithString(line, "a=fmtp:63 ")) {
            offer_audio_red_fmtp = line;
        }
    }
    std::vector<std::string> lines = SplitSdpLines(answer_sdp);
    std::vector<std::string> out;
    bool in_video = false;
    bool in_audio = false;
    bool video_bitrate_added = false;
    bool video_feedback_added = false;
    bool audio_red_added = false;

    const std::string payload = std::to_string(h264_payload_type);

    for (const auto& line : lines) {
        if (StartsWith(line, "m=")) {
            in_video = StartsWith(line, "m=video");
            in_audio = StartsWith(line, "m=audio");
            if (in_video) {
                out.push_back("m=video 9 UDP/TLS/RTP/SAVPF " + payload);
                continue;
            } else if (in_audio) {
                out.push_back(offer_audio_red_rtpmap.empty()
                    ? "m=audio 9 UDP/TLS/RTP/SAVPF 111"
                    : "m=audio 9 UDP/TLS/RTP/SAVPF 111 63");
                continue;
            } else if (StartsWith(line, "m=application")) {
                out.push_back("m=application 9 UDP/DTLS/SCTP webrtc-datachannel");
                continue;
            }
        }

        if (in_audio && !audio_red_added && StartsWith(line, "a=rtpmap:111")) {
            out.push_back(line);
            if (!offer_audio_red_rtpmap.empty()) {
                out.push_back(offer_audio_red_rtpmap);
                if (!offer_audio_red_fmtp.empty())
                    out.push_back(offer_audio_red_fmtp);
            }
            audio_red_added = true;
            continue;
        }

        if (in_video && StartsWith(line, "c=IN ")) {
            out.push_back(line);
            out.push_back("b=AS:" + std::to_string(settings.bitrate_kbps));
            video_bitrate_added = true;
            continue;
        }

        if (in_video && StartsWith(line, "a=rtcp-fb:96")) {
            if (!video_feedback_added) {
                if (!offer_h264_feedback.empty()) {
                    for (const auto& feedback : offer_h264_feedback)
                        out.push_back(feedback);
                } else {
                    out.push_back("a=rtcp-fb:" + payload + " nack");
                    out.push_back("a=rtcp-fb:" + payload + " nack pli");
                    out.push_back("a=rtcp-fb:" + payload + " transport-cc");
                    out.push_back("a=rtcp-fb:" + payload + " ccm fir");
                }
                video_feedback_added = true;
            }
            continue;
        }

        if (in_video && StartsWith(line, "a=fmtp:96")) {
            if (!offer_h264_fmtp.empty())
                out.push_back(offer_h264_fmtp);
            else
                out.push_back("a=fmtp:" + payload + " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
            continue;
        }

        if (in_video && StartsWith(line, "a=rtpmap:96")) {
            out.push_back("a=rtpmap:" + payload + " H264/90000");
            continue;
        }

        if (in_video && StartsWith(line, "a=ssrc:"))
            continue;

        if (in_video && StartsWith(line, "a=sendrecv")) {
            out.push_back("a=recvonly");
            continue;
        }

        out.push_back(line);
    }

    if (!video_bitrate_added) {
        for (size_t i = 0; i < out.size(); ++i) {
            if (StartsWith(out[i], "m=video")) {
                out.insert(out.begin() + static_cast<long>(i + 1), "b=AS:" + std::to_string(settings.bitrate_kbps));
                break;
            }
        }
    }

    return AlignAnswerSdpToOffer(JoinSdpLines(out), offer_sdp);
}

std::string ExtractSdpValue(const std::string& sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(sdp)) {
        if (StartsWithString(line, prefix))
            return line.substr(prefix.size());
    }
    return "";
}

int ExtractNvstIntValue(const std::string& nvst_sdp, const std::string& prefix)
{
    for (const auto& line : SplitSdpLines(nvst_sdp)) {
        if (!StartsWithString(line, prefix))
            continue;

        const std::string value = line.substr(prefix.size());
        char* end = nullptr;
        const long parsed = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str() && parsed > 0 && parsed <= 65535)
            return static_cast<int>(parsed);
    }
    return 0;
}

std::string ExtractSignalingHost(const std::string& url)
{
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;

    size_t end = url.find('/', start);
    std::string host_port = url.substr(start, end == std::string::npos ? std::string::npos : end - start);

    const size_t at = host_port.rfind('@');
    if (at != std::string::npos)
        host_port.erase(0, at + 1);

    if (!host_port.empty() && host_port.front() == '[') {
        const size_t close = host_port.find(']');
        return close == std::string::npos ? host_port : host_port.substr(1, close - 1);
    }

    const size_t colon = host_port.find(':');
    return colon == std::string::npos ? host_port : host_port.substr(0, colon);
}

std::string DottedIpv4FromGfnHost(const std::string& host)
{
    int octets[4] = {-1, -1, -1, -1};
    size_t pos = 0;

    for (int i = 0; i < 4; ++i) {
        if (pos >= host.size() || !std::isdigit(static_cast<unsigned char>(host[pos])))
            return "";

        int value = 0;
        while (pos < host.size() && std::isdigit(static_cast<unsigned char>(host[pos]))) {
            value = value * 10 + (host[pos] - '0');
            ++pos;
        }

        if (value < 0 || value > 255)
            return "";

        octets[i] = value;
        if (i < 3) {
            if (pos >= host.size() || host[pos] != '-')
                return "";
            ++pos;
        }
    }

    return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
           std::to_string(octets[2]) + "." + std::to_string(octets[3]);
}

std::string NormalizeGfnMediaIp(const std::string& host)
{
    std::string media_ip = DottedIpv4FromGfnHost(host);
    return media_ip.empty() ? host : media_ip;
}

std::string PrepareGfnOfferSdp(
    std::string sdp,
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    [[maybe_unused]] int media_port_hint)
{
    std::string media_ip = NormalizeGfnMediaIp(media_ip_hint);

    if (media_ip.empty()) {
        const std::string host = ExtractSignalingHost(signaling_url);
        media_ip = NormalizeGfnMediaIp(host);
    }

    if (!media_ip.empty())
        sdp = ReplaceAll(std::move(sdp), "0.0.0.0", media_ip);

    return sdp;
}

std::string BuildManualMediaCandidate(
    const std::string& signaling_url,
    const std::string& media_ip_hint,
    int media_port_hint,
    int foundation)
{
    std::string media_ip = NormalizeGfnMediaIp(media_ip_hint);
    if (media_ip.empty()) {
        const std::string host = ExtractSignalingHost(signaling_url);
        media_ip = NormalizeGfnMediaIp(host);
    }

    if (media_ip.empty() || media_port_hint <= 0)
        return "";

    return "a=candidate:" + std::to_string(foundation) + " 1 UDP 2130706431 " + media_ip + " " +
        std::to_string(media_port_hint) + " typ host";
}


} // namespace

namespace opennow::webrtc::internal
{

extern "C" {
    void on_ice_candidate_cb(char* sdp_text, void* userdata) {
        if (userdata && sdp_text) {
            static_cast<WebRtcSession*>(userdata)->on_ice_candidate(std::string(sdp_text));
        }
    }

    void on_peer_state_change_cb(PeerConnectionState state, void* userdata) {
        if (userdata) {
            static_cast<WebRtcSession*>(userdata)->on_peer_state_change(state);
        }
    }

    void on_datachannel_message_cb(char* msg, size_t len, void* userdata, uint16_t sid) {
        if (userdata) {
            static_cast<WebRtcSession*>(userdata)->on_datachannel_message(msg, len, sid);
        }
    }

    void on_datachannel_open_cb(void* userdata) {
        if (userdata) {
            static_cast<WebRtcSession*>(userdata)->on_datachannel_open();
        }
    }

    void on_datachannel_close_cb(void* userdata) {
        if (userdata) {
            static_cast<WebRtcSession*>(userdata)->on_datachannel_close();
        }
    }

    void on_video_packet_cb(const PeerVideoPacket* packet, void* userdata) {
        if (userdata && packet)
            static_cast<WebRtcSession*>(userdata)->on_video_packet(*packet);
    }

    void on_audio_packet_cb(const PeerAudioPacket* packet, void* userdata) {
        if (userdata && packet)
            static_cast<WebRtcSession*>(userdata)->on_audio_packet(*packet);
    }

    void on_rtp_sender_report_cb(uint32_t ssrc, uint64_t ntp_us,
                                        uint32_t rtp_timestamp, void* userdata) {
        if (userdata)
            static_cast<WebRtcSession*>(userdata)->on_rtp_sender_report(ssrc, ntp_us, rtp_timestamp);
    }
}

} // namespace opennow::webrtc::internal

void WebRtcSession::handle_signaling_message(const std::string& msg) {
    signaling_rx_count_++;
    AppendStreamLog("RX " + msg);
    const std::string compact_message = CompactSignalingMessage(msg);
    AppendTraceLog(compact_message);

    if (last_messages_.size() >= 3) {
        last_messages_.erase(last_messages_.begin());
    }
    last_messages_.push_back(compact_message);

    json_error_t error;
    json_t* root = json_loads(msg.c_str(), 0, &error);
    if(opennow::signaling::ObserveRegistration(signaling_registration_,root,peer_name_))
        AppendStreamLog("SIGNAL_DIAG registration_confirmed attempts=" +
            std::to_string(signaling_registration_.Attempts()) + " furtherPeerInfo=disabled");
    const auto safe_envelope=signaling_diagnostics_.Observe(root,peer_name_);
    if(!safe_envelope.empty()) AppendStreamLog(safe_envelope);
    if (!root) {
        AppendStreamLog("SIGNAL envelope parse_ok=0 bytes=" + std::to_string(msg.size()));
        return;
    }
    if (signaling_rx_count_ <= 16 || json_object_get(root, "peer_msg"))
        AppendStreamLog("SIGNAL envelope parse_ok=1 peer_info=" +
            std::to_string(json_object_get(root, "peer_info") != nullptr) +
            " peer_msg=" + std::to_string(json_object_get(root, "peer_msg") != nullptr) +
            " heartbeat=" + std::to_string(json_object_get(root, "hb") != nullptr));

    json_t* peer_info = json_object_get(root, "peer_info");
    if (peer_info && json_is_object(peer_info)) {
        const char* name = json_string_value(json_object_get(peer_info, "name"));
        json_t* id = json_object_get(peer_info, "id");
        if (name && peer_name_ == name && json_is_integer(id)) {
            peer_id_ = static_cast<int>(json_integer_value(id));
            current_state_ = "Signaling peer id assigned";
        }
    }

    // The server's own-identity notification is reliable too. Skipping its ACK
    // leaves it pending even though our outgoing registration was confirmed.
    if (const auto ackid = opennow::signaling::IncomingAckId(root)) {
        json_t* ack = json_object();
        json_object_set_new(ack, "ack", json_integer(*ackid));
        char* dump = json_dumps(ack, 0);
        const bool serialized = dump != nullptr;
        bool written = false;
        if (dump) {
            written = signaling_client_->send_message(std::string(dump));
            free(dump);
        }
        const auto safe_ack = signaling_diagnostics_.Ack(false, serialized, written);
        if (!safe_ack.empty()) AppendStreamLog(safe_ack);
        json_decref(ack);
    }

    if (json_object_get(root, "hb")) {
        heartbeat_rx_count_++;
        json_t* heartbeat = json_object();
        json_object_set_new(heartbeat, "hb", json_integer(1));
        char* dump = json_dumps(heartbeat, 0);
        if (dump) {
            heartbeat_tx_count_++;
            AppendStreamLog("TX " + std::string(dump));
            signaling_client_->send_message(std::string(dump));
            free(dump);
        }
        json_decref(heartbeat);
        json_decref(root);
        return;
    }

    json_t* peer_msg = json_object_get(root, "peer_msg");
    if (peer_msg && json_is_object(peer_msg)) {
        json_t* from = json_object_get(peer_msg, "from");
        if (json_is_integer(from))
            remote_peer_id_ = static_cast<int>(json_integer_value(from));

        json_t* raw_payload = json_object_get(peer_msg, "msg");
        if (raw_payload && json_is_string(raw_payload)) {
            json_error_t payload_error;
            json_t* payload = json_loads(json_string_value(raw_payload), 0, &payload_error);
            AppendStreamLog(opennow::signaling::DescribePayload(payload,payload!=nullptr,json_string_length(raw_payload)));
            if (payload) {
                json_t* type = json_object_get(payload, "type");
                const auto kind = type && json_is_string(type) ? std::string(json_string_value(type)) : "missing";
                AppendStreamLog("SIGNAL payload kind=" +
                    std::string(kind == "offer" || kind == "answer" || kind == "candidate" ? kind : "other") +
                    " has_sdp=" + std::to_string(json_is_string(json_object_get(payload, "sdp"))));
                if (type && json_is_string(type) && std::string(json_string_value(type)) == "offer") {
                    json_t* sdp = json_object_get(payload, "sdp");
                    if (sdp && json_is_string(sdp) && pc_) {
                        offer_received_ = true;
                        offer_count_++;
                        current_state_ = "Got offer, sending answer";
                        const std::string raw_offer_sdp = json_string_value(sdp);
                        AppendTraceBlock("RAW OFFER SDP", raw_offer_sdp);
                        const std::string offer_sdp =
                            PrepareGfnOfferSdp(raw_offer_sdp, signaling_url_, media_ip_, media_port_);
                        AppendTraceBlock("PREPARED OFFER SDP", offer_sdp);
                        json_t* nvst_offer = json_object_get(payload, "nvstSdp");
                        const std::string nvst_offer_sdp =
                            nvst_offer && json_is_string(nvst_offer) ? json_string_value(nvst_offer) : "";
                        if (!nvst_offer_sdp.empty())
                            AppendTraceBlock("OFFER NVST SDP", nvst_offer_sdp);
                        const int offered_bitrate_cap = ExtractNvstIntValue(
                            nvst_offer_sdp, "a=vqos.bw.maximumBitrateKbps:");
                        const int offered_initial_bitrate = ExtractNvstIntValue(
                            nvst_offer_sdp, "a=video.initialBitrateKbps:");
                        AppendStreamLog("BITRATE negotiation requestedKbps=" +
                            std::to_string(settings_.bitrate_kbps) +
                            " offerInitialKbps=" +
                            std::to_string(offered_initial_bitrate) +
                            " offerMaximumKbps=" +
                            std::to_string(offered_bitrate_cap) +
                            " offerBelowRequest=" + std::to_string(
                                offered_bitrate_cap > 0 &&
                                offered_bitrate_cap < settings_.bitrate_kbps ? 1 : 0));
                        server_ice_ufrag_ = ExtractSdpValue(offer_sdp, "a=ice-ufrag:");
                        const opennow::webrtc::RiInputCapabilities ri_caps =
                            ParseRiInputCapabilities(offer_sdp);
                        partial_reliable_threshold_ms_ = ri_caps.partial_reliable_threshold_ms;
                        const int offer_video_port = ExtractOfferMediaPort(offer_sdp, "video");
                        const int selected_h264_pt = SelectOfferH264PayloadType(offer_sdp);
                        AppendStreamLog("SDP offer received count=" + std::to_string(offer_count_) +
                                        " iceUfrag=" + server_ice_ufrag_ +
                                        " videoPort=" + std::to_string(offer_video_port) +
                                        " h264Pt=" + std::to_string(selected_h264_pt) +
                                        " remoteCandidates=" + std::to_string(CountSdpLinesWithPrefix(offer_sdp, "a=candidate:")) +
                                        " riThreshold=" + std::to_string(ri_caps.partial_reliable_threshold_ms) +
                                        " riGamepadMask=" + std::to_string(ri_caps.partial_reliable_gamepad_mask) +
                                        " riHidMask=" + std::to_string(ri_caps.hid_device_mask));
                        AppendInputLog("CAPS offer partialThresholdMs=" +
                                       std::to_string(ri_caps.partial_reliable_threshold_ms) +
                                       " gamepadMask=" + std::to_string(ri_caps.partial_reliable_gamepad_mask) +
                                       " hidMask=" + std::to_string(ri_caps.hid_device_mask) +
                                       " applicationMLine=" +
                                       std::to_string(offer_sdp.find("m=application") != std::string::npos ? 1 : 0));
                        peer_connection_set_remote_description(pc_, offer_sdp.c_str(), SDP_TYPE_OFFER);
                        remote_description_set_ = true;
                        const char* answer_sdp = peer_connection_create_answer(pc_);
                        if (answer_sdp) {
                            AppendTraceBlock("LIBPEER RAW ANSWER SDP", answer_sdp);
                            const std::string adapted_answer_sdp = AdaptAnswerSdpToOffer(answer_sdp, offer_sdp, settings_);
                            const std::string nvst_sdp = opennow::webrtc::BuildNvstSdp(
                                adapted_answer_sdp, settings_, ri_caps);
                            AppendTraceBlock("ADAPTED ANSWER SDP", adapted_answer_sdp);
                            AppendTraceBlock("ANSWER NVST SDP", nvst_sdp);
                            AppendStreamLog("SDP answer created localUfrag=" +
                                            ExtractSdpValue(adapted_answer_sdp, "a=ice-ufrag:") +
                                            " localCandidates=" +
                                            std::to_string(CountSdpLinesWithPrefix(adapted_answer_sdp, "a=candidate:")) +
                                            " h264Pt=" + std::to_string(selected_h264_pt));
                            json_t* answer = json_object();
                            json_object_set_new(answer, "type", json_string("answer"));
                            json_object_set_new(answer, "sdp", json_string(adapted_answer_sdp.c_str()));
                            json_object_set_new(answer, "nvstSdp", json_string(nvst_sdp.c_str()));
                            send_peer_payload(answer);
                            answer_sent_ = true;
                            json_decref(answer);
                            flush_pending_local_candidates();
                            schedule_manual_media_candidates(offer_sdp, nvst_offer_sdp);
                        }
                    }
                } else {
                    json_t* candidate = json_object_get(payload, "candidate");
                    if (candidate && json_is_string(candidate) && pc_) {
                        std::string candidate_text = json_string_value(candidate);
                        if (candidate_text.rfind("a=candidate:", 0) != 0)
                            candidate_text = "a=" + candidate_text;
                        AppendStreamLog("REMOTE trickle-candidate " + candidate_text);
                        if (peer_connection_add_ice_candidate(pc_, candidate_text.data()) == 0) {
                            remote_trickle_received_ = true;
                            remote_ice_count_++;
                            current_state_ = "Got remote ICE";
                        } else {
                            current_state_ = "Remote ICE rejected";
                            AppendStreamLog("REMOTE trickle-candidate rejected " + candidate_text);
                        }
                    }
                }
                json_decref(payload);
            }
        }
    }
    json_decref(root);
}

void WebRtcSession::on_ice_candidate(const std::string& sdp) {
    if (!signaling_client_)
        return;

    if (sdp.find("v=0") != std::string::npos || sdp.find("m=") != std::string::npos) {
        queue_local_candidates_from_sdp(sdp);
        if (answer_sent_)
            flush_pending_local_candidates();
        return;
    }

    send_local_candidate(sdp);
}

void WebRtcSession::on_peer_state_change(PeerConnectionState state) {
    current_state_ = std::string("Peer ") + peer_connection_state_to_string(state);
    AppendStreamLog("PEER state " + std::string(peer_connection_state_to_string(state)));
    AppendInputLog("TRANSPORT peerState=" + std::string(peer_connection_state_to_string(state)));
    if (state == PEER_CONNECTION_COMPLETED && !peer_completed_seen_) {
        peer_terminal_.store(false, std::memory_order_release);
        peer_ever_completed_.store(true, std::memory_order_release);
        peer_terminal_kind_.store(
            static_cast<int>(opennow::PeerTerminalKind::None),
            std::memory_order_release);
        peer_completed_seen_ = true;
        peer_completed_at_ = std::chrono::steady_clock::now();
        AppendStreamLog("STREAM transport_completed waiting_for_first_video_packet");
    }
    if (state == PEER_CONNECTION_FAILED || state == PEER_CONNECTION_CLOSED || state == PEER_CONNECTION_DISCONNECTED) {
        peer_terminal_.store(true, std::memory_order_release);
        opennow::PeerTerminalKind terminal_kind = opennow::PeerTerminalKind::Closed;
        if (state == PEER_CONNECTION_FAILED)
            terminal_kind = opennow::PeerTerminalKind::Failed;
        else if (state == PEER_CONNECTION_DISCONNECTED)
            terminal_kind = opennow::PeerTerminalKind::Disconnected;
        peer_terminal_kind_.store(static_cast<int>(terminal_kind), std::memory_order_release);
        network_running_.store(false, std::memory_order_release);
        log_stream_summary("peer_state_terminal");
    }
}


void WebRtcSession::queue_local_candidates_from_sdp(const std::string& sdp) {
    for (const auto& line : SplitSdpLines(sdp)) {
        if (!StartsWithString(line, "a=candidate:"))
            continue;

        bool seen = false;
        for (const auto& candidate : pending_local_candidates_) {
            if (candidate == line) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            pending_local_candidates_.push_back(line);
            AppendStreamLog("LOCAL queued-candidate " + line);
        }
    }
}

void WebRtcSession::send_local_candidate(const std::string& candidate_sdp) {
    if (!signaling_client_ || candidate_sdp.empty())
        return;

    std::string candidate = candidate_sdp;
    if (candidate.rfind("a=candidate:", 0) == 0)
        candidate.erase(0, 2);
    while (!candidate.empty() && (candidate.back() == '\r' || candidate.back() == '\n'))
        candidate.pop_back();

    json_t* req = json_object();
    json_object_set_new(req, "candidate", json_string(candidate.c_str()));
    json_object_set_new(req, "sdpMid", json_string("0"));
    json_object_set_new(req, "sdpMLineIndex", json_integer(0));
    AppendStreamLog("LOCAL trickle-candidate " + candidate);
    AppendTraceLog("LOCAL trickle-candidate mid=0 " + candidate);
    send_peer_payload(req);
    json_decref(req);
    local_ice_count_++;
}

void WebRtcSession::flush_pending_local_candidates() {
    if (!answer_sent_ || pending_local_candidates_.empty())
        return;

    std::vector<std::string> candidates;
    candidates.swap(pending_local_candidates_);
    for (const auto& candidate : candidates)
        send_local_candidate(candidate);
}

void WebRtcSession::schedule_manual_media_candidates(const std::string& offer_sdp, const std::string& nvst_sdp) {
    if (manual_candidate_added_)
        return;

    // CloudMatch can assign a per-session NVST port that differs from the
    // bundle ports advertised in the SDP. Prefer that endpoint: some bundle
    // ports answer ICE but never accept the DTLS media handshake.
    const std::vector<int> ports = opennow::stream::PrioritizeRemoteCandidatePorts(
        media_port_,
        {ExtractOfferMediaPort(offer_sdp, "video"),
         ExtractOfferMediaPort(offer_sdp, "audio"),
         ExtractOfferMediaPort(offer_sdp, "application"),
         ExtractNvstIntValue(nvst_sdp, "a=general.serverBundlePort:")});

    pending_manual_candidates_.clear();
    int foundation = 1;
    for (int port : ports) {
        std::string candidate = BuildManualMediaCandidate(signaling_url_, media_ip_, port, foundation++);
        if (!candidate.empty())
            pending_manual_candidates_.push_back(candidate);
    }

    if (pending_manual_candidates_.empty())
        return;

    manual_candidate_due_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    std::string port_list;
    for (int port : ports) {
        if (!port_list.empty())
            port_list += ",";
        port_list += std::to_string(port);
    }
    AppendStreamLog("LOCAL delayed-manual-candidates scheduled ufrag=" + server_ice_ufrag_ +
                    " ports=" + port_list +
                    " count=" + std::to_string(pending_manual_candidates_.size()));
}

void WebRtcSession::maybe_add_manual_media_candidate() {
    if (!pc_ || manual_candidate_added_ || pending_manual_candidates_.empty())
        return;

    const PeerConnectionState state = peer_connection_get_state(pc_);
    if (state == PEER_CONNECTION_COMPLETED) {
        pending_manual_candidates_.clear();
        return;
    }

    if (manual_candidate_due_.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() < manual_candidate_due_) {
        return;
    }

    // Prefer NVIDIA's trickled endpoint first. This fallback is intentionally
    // delayed so it does not block the real media candidate in libpeer.
    AppendStreamLog("LOCAL delayed-manual-candidates adding ufrag=" + server_ice_ufrag_ +
                    " remoteTrickle=" + std::to_string(remote_trickle_received_ ? 1 : 0) +
                    " count=" + std::to_string(pending_manual_candidates_.size()));
    int added = 0;
    for (auto& candidate : pending_manual_candidates_) {
        if (peer_connection_add_ice_candidate(pc_, candidate.data()) == 0) {
            added++;
            remote_ice_count_++;
            AppendStreamLog("LOCAL delayed-manual-candidate add-ok " + candidate);
        } else {
            AppendStreamLog("LOCAL delayed-manual-candidate rejected " + candidate);
        }
    }

    pending_manual_candidates_.clear();
    manual_candidate_added_ = true;
    if (added > 0) {
        current_state_ = "Added fallback ICE";
    } else {
        current_state_ = "Fallback ICE rejected";
    }
}
