#include "cloud_session_protocol.hpp"

#include "app_launch_mode_policy.hpp"

#include <jansson.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <charconv>
#include <limits>
#include <string>
#include <stdexcept>

namespace opennow::gfn::cloud_session
{

ClientProfile NewClientProfile(const std::string& provider_code) {
    return provider_code=="NVIDIA" ? ClientProfile::BrowserWebRtc : ClientProfile::NativeWebRtc;
}
const char* ProfileName(ClientProfile profile) {
    return profile==ClientProfile::BrowserWebRtc ? "browser_webrtc_v1" : "native_webrtc_v1";
}
ClientProfile ReadClientProfile(json_t* record) {
    if(!record) return ClientProfile::BrowserWebRtc; // New, untracked session.
    if(!json_is_object(record)) throw std::runtime_error("Invalid saved CloudMatch context");
    auto* value=json_object_get(record,"cloudmatch_profile");
    if(!value) return ClientProfile::NativeWebRtc; // Pre-R50 saved allocation.
    if(json_is_string(value)) {
        const std::string name=json_string_value(value);
        if(name=="browser_webrtc_v1") return ClientProfile::BrowserWebRtc;
        if(name=="native_webrtc_v1") return ClientProfile::NativeWebRtc;
    }
    throw std::runtime_error("Unsupported saved CloudMatch profile");
}
void StoreClientProfile(json_t* record, ClientProfile profile) {
    if(!json_is_object(record)) throw std::runtime_error("Invalid CloudMatch context");
    json_object_set_new(record,"cloudmatch_profile",json_string(ProfileName(profile)));
}
std::vector<std::string> BuildSessionHeaders(const std::string& token, const std::string& client_id,
    const std::string& device_id, ClientProfile profile) {
    if(token.empty() || client_id.empty() || device_id.empty() ||
        token.find_first_of("\r\n")!=std::string::npos || client_id.find_first_of("\r\n")!=std::string::npos ||
        device_id.find_first_of("\r\n")!=std::string::npos)
        throw std::runtime_error("Invalid CloudMatch header identity");
    const bool browser=profile==ClientProfile::BrowserWebRtc;
    return {"Authorization: GFNJWT "+token, "Content-Type: application/json", "nv-client-id: "+client_id,
        std::string("nv-client-type: ")+(browser ? "BROWSER" : "NATIVE"),
        std::string("nv-client-streamer: ")+(browser ? "WEBRTC" : "NVIDIA-CLASSIC"),
        "nv-client-version: 30.0", "nv-browser-type: CHROME", "nv-browser-version: 131.0.0.0",
        "nv-device-make: UNKNOWN", "nv-device-model: UNKNOWN", "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP", "x-device-id: "+device_id,
        "Origin: https://play.geforcenow.com", "Referer: https://play.geforcenow.com/"};
}
namespace {
int QueueNumber(json_t* value) {
    if (json_is_integer(value)) {
        const auto number = json_integer_value(value);
        return number >= 0 && number <= std::numeric_limits<int>::max()
            ? static_cast<int>(number) : -1;
    }
    if (!json_is_string(value)) return -1;
    const std::string text = json_string_value(value);
    int number = -1;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && number >= 0
        ? number : -1;
}
}

void ApplyQueueState(json_t* response, SessionInfo& info) {
    auto* session = json_object_get(response, "session");
    info.seat_setup_step = QueueNumber(json_object_get(json_object_get(session, "seatSetupInfo"), "seatSetupStep"));
    // The detailed seat state is authoritative, including an explicit zero.
    // Root-level compatibility fields must not overwrite it with a stale value.
    const std::pair<json_t*, const char*> sources[] = {
        {json_object_get(session, "seatSetupInfo"), "seat_setup"},
        {session, "session"},
        {json_object_get(session, "sessionProgress"), "session_progress"},
        {json_object_get(session, "progressInfo"), "progress_info"},
        {response, "root"},
    };
    info.queue_position = -1;
    info.queue_source = "missing";
    info.queue_conflict = false;
    for (const auto& [object, source] : sources) {
        auto* value = json_object_get(object, "queuePosition");
        if (!value) continue;
        const int number = QueueNumber(value);
        if (info.queue_source == "missing") {
            info.queue_position = number;
            info.queue_source = source;
        } else if (number != info.queue_position) {
            info.queue_conflict = true;
        }
    }
}

void StoreRoutingIdentity(json_t* record, const opennow::recovery::Identity& identity) {
    if (!json_is_object(record) || identity.user.empty() || identity.provider_url.empty() ||
        identity.device.empty() || identity.client.empty())
        throw std::runtime_error("Incomplete cloud session identity");
    json_object_set_new(record,"user_id",json_string(identity.user.c_str()));
    json_object_set_new(record,"provider_id",json_string(identity.provider.c_str()));
    json_object_set_new(record,"provider_url",json_string(identity.provider_url.c_str()));
    json_object_set_new(record,"device_id",json_string(identity.device.c_str()));
    json_object_set_new(record,"client_id",json_string(identity.client.c_str()));
}

std::string BuildResumeBody(const std::string& app_id, const std::string& device_id,
    const std::string& sub_session_id, ClientProfile profile)
{
    // Desktop claim protocol: immutable FPS/resolution/codec are absent.
    static constexpr const char* body = R"({"action":2,"data":"RESUME","metaData":[],
      "sessionRequestData":{"audioMode":2,"remoteControllersBitmap":0,"sdrHdrMode":0,
      "networkTestSessionId":null,"availableSupportedControllers":[],"clientVersion":"30.0",
      "internalTitle":null,"clientPlatformName":"windows","surroundAudioInfo":0,
      "clientTimezoneOffset":0,"clientIdentification":"GFN-PC","parentSessionId":null,
      "streamerVersion":1,"appLaunchMode":1,"sdkVersion":"1.0","enhancedStreamMode":1,
      "useOps":true,"clientDisplayHdrCapabilities":null,"accountLinked":true,
      "partnerCustomData":"","enablePersistingInGameSettings":true,
      "secureRTSPSupported":false,"userAge":26,"metaData":[
      {"key":"wssignaling","value":"1"},{"key":"GSStreamerType","value":"WebRTC"},
      {"key":"networkType","value":"Unknown"},{"key":"ClientImeSupport","value":"0"},
      {"key":"surroundAudioInfo","value":"2"}]}})";
    if (app_id.empty() || device_id.empty() || sub_session_id.empty() ||
        !std::all_of(app_id.begin(), app_id.end(), [](unsigned char c) { return std::isdigit(c); }))
        throw std::runtime_error("Invalid resume identity");
    const auto numeric_id = std::stoll(app_id);
    if (numeric_id <= 0) throw std::runtime_error("Invalid resume app ID");
    json_t* root = json_loads(body, 0, nullptr);
    if (!root) throw std::runtime_error("Cannot build session resume request");
    json_t* req = json_object_get(root, "sessionRequestData");
    json_object_set_new(req, "appId", json_integer(numeric_id));
    // Keep SwitchNOW's original gamepad-friendly launcher mode on resume.
    json_object_set_new(req, "appLaunchMode", json_integer(ConsoleAppLaunchModeWireValue()));
    json_object_set_new(req, "clientPlatformName", json_string(
        profile==ClientProfile::BrowserWebRtc ? "browser" : "windows"));
    json_object_set_new(req, "deviceHashId", json_string(device_id.c_str()));
    json_array_append_new(json_object_get(req, "metaData"),
        json_pack("{s:s,s:s}", "key", "SubSessionId", "value", sub_session_id.c_str()));
    char* output = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!output) throw std::runtime_error("Cannot serialize session resume request");
    std::string result(output); std::free(output); return result;
}

std::string SubSessionIdFromRandom(const std::vector<uint8_t>& random) {
    if(random.size()!=16) throw std::runtime_error("Invalid sub-session entropy length");
    auto bytes=random;
    bytes[6]=(bytes[6]&0x0f)|0x40;
    bytes[8]=(bytes[8]&0x3f)|0x80;
    static constexpr char hex[]="0123456789abcdef";
    std::string id;id.reserve(36);
    for(size_t i=0;i<bytes.size();++i) {
        if(i==4 || i==6 || i==8 || i==10) id+='-';
        id+=hex[bytes[i]>>4];id+=hex[bytes[i]&15];
    }
    return id;
}

std::string BuildSessionBody(
    const std::string& app_id,
    const std::string& internal_title,
    const std::string& device_id,
    const opennow::StreamSettings& stream_settings,
    const std::string& sub_session_id, ClientProfile profile)
{
    if(sub_session_id.empty() || sub_session_id==device_id)
        throw std::runtime_error("A fresh sub-session identity is required");
    json_t* root = json_object();
    json_t* req = json_object();
    try {
        json_object_set_new(req, "appId", json_integer(std::stoll(app_id)));
    } catch (...) {
        json_object_set_new(req, "appId", json_integer(0));
    }
    json_object_set_new(req, "cmsId", json_string(app_id.c_str()));
    if (internal_title.empty())
        json_object_set_new(req, "internalTitle", json_null());
    else
        json_object_set_new(req, "internalTitle", json_string(internal_title.c_str()));
    json_object_set_new(req, "networkTestSessionId", json_null());
    json_object_set_new(req, "parentSessionId", json_null());
    json_object_set_new(req, "clientIdentification", json_string("GFN-PC"));
    json_object_set_new(req, "deviceHashId", json_string(device_id.c_str()));
    json_object_set_new(req, "clientVersion", json_string("30.0"));
    json_object_set_new(req, "clientPlatformName", json_string(
        profile==ClientProfile::BrowserWebRtc ? "browser" : "windows"));

    json_object_set_new(req, "availableSupportedControllers", json_array());
    json_object_set_new(req, "sdkVersion", json_string("1.0"));
    json_object_set_new(req, "streamerVersion", json_integer(1));
    json_object_set_new(req, "useOps", json_true());
    json_object_set_new(req, "audioMode", json_integer(2));
    json_object_set_new(req, "sdrHdrMode", json_integer(0));
    json_object_set_new(req, "clientDisplayHdrCapabilities", json_null());
    json_object_set_new(req, "surroundAudioInfo", json_integer(0));
    // Advertise four XInput slots. Per-report bitmaps still describe which
    // players are currently connected, including NVIDIA's mirrored high byte.
    json_object_set_new(req, "remoteControllersBitmap", json_integer(0x0f0f));
    json_object_set_new(req, "enhancedStreamMode", json_integer(1));
    json_object_set_new(
        req, "appLaunchMode",
        json_integer(opennow::gfn::ConsoleAppLaunchModeWireValue()));
    json_object_set_new(req, "secureRTSPSupported", json_false());
    json_object_set_new(req, "partnerCustomData", json_string(""));
    json_object_set_new(req, "accountLinked", json_true());
    json_object_set_new(
        req, "enablePersistingInGameSettings",
        json_boolean(stream_settings.persist_game_settings));
    json_object_set_new(req, "userAge", json_integer(26));
    const std::time_t now = std::time(nullptr);
    std::tm local_time {};
    std::tm utc_time {};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
    gmtime_s(&utc_time, &now);
#else
    localtime_r(&now, &local_time);
    gmtime_r(&now, &utc_time);
#endif
    utc_time.tm_isdst = local_time.tm_isdst;
    const std::time_t local_epoch = std::mktime(&local_time);
    const std::time_t utc_as_local_epoch = std::mktime(&utc_time);
    const auto timezone_offset_ms = static_cast<json_int_t>(
        std::difftime(local_epoch, utc_as_local_epoch) * 1000.0);
    json_object_set_new(
        req, "clientTimezoneOffset", json_integer(timezone_offset_ms));
    
    json_t* features = json_object();
    json_object_set_new(features, "reflex", json_false());
    // CloudMatch uses an enum here: 0 = 8-bit SDR, 10 = 10-bit capable.
    // Sending literal 8 makes the whole launch request fail with a generic
    // INTERNAL_ERROR_STATUS before the session can enter queue/setup.
    json_object_set_new(features, "bitDepth", json_integer(0));
    json_object_set_new(features, "cloudGsync", json_false());
    json_object_set_new(features, "enabledL4S", json_false());
    json_object_set_new(features, "mouseMovementFlags", json_integer(0));
    json_object_set_new(features, "trueHdr", json_false());
    json_object_set_new(features, "supportedHidDevices", json_integer(0));
    json_object_set_new(features, "profile", json_integer(0));
    json_object_set_new(features, "fallbackToLogicalResolution", json_false());
    json_object_set_new(features, "hidDevices", json_null());
    json_object_set_new(features, "chromaFormat", json_integer(0));
    json_object_set_new(features, "prefilterMode", json_integer(0));
    json_object_set_new(features, "prefilterSharpness", json_integer(0));
    json_object_set_new(features, "prefilterNoiseReduction", json_integer(0));
    json_object_set_new(features, "hudStreamingMode", json_integer(0));
    json_object_set_new(features, "sdrColorSpace", json_integer(2));
    json_object_set_new(features, "hdrColorSpace", json_integer(0));
    json_object_set_new(features, "maxBitrateKbps", json_integer(stream_settings.bitrate_kbps));
    json_object_set_new(features, "codec", json_integer(1));
    json_object_set_new(features, "vsync", json_false());
    json_object_set_new(features, "dynamicStreamingMode", json_integer(0));
    json_object_set_new(features, "audioChannelCount", json_integer(2));
    json_object_set_new(req, "requestedStreamingFeatures", features);

    json_t* meta = json_array();
    json_t* m1 = json_object(); json_object_set_new(m1, "key", json_string("SubSessionId")); json_object_set_new(m1, "value", json_string(sub_session_id.c_str())); json_array_append_new(meta, m1);
    json_t* m2 = json_object(); json_object_set_new(m2, "key", json_string("wssignaling")); json_object_set_new(m2, "value", json_string("1")); json_array_append_new(meta, m2);
    json_t* m3 = json_object(); json_object_set_new(m3, "key", json_string("GSStreamerType")); json_object_set_new(m3, "value", json_string("WebRTC")); json_array_append_new(meta, m3);
    json_object_set_new(req, "metaData", meta);

    json_t* monitors = json_array();
    json_t* monitor = json_object();
    json_object_set_new(monitor, "monitorId", json_integer(0));
    json_object_set_new(monitor, "positionX", json_integer(0));
    json_object_set_new(monitor, "positionY", json_integer(0));
    json_object_set_new(monitor, "widthInPixels", json_integer(stream_settings.width));
    json_object_set_new(monitor, "heightInPixels", json_integer(stream_settings.height));
    json_object_set_new(monitor, "framesPerSecond", json_integer(stream_settings.fps));
    json_object_set_new(monitor, "sdrHdrMode", json_integer(0));
    json_object_set_new(monitor, "displayData", json_null());
    json_object_set_new(monitor, "hdr10PlusGamingData", json_null());
    json_object_set_new(monitor, "dpi", json_integer(100));
    json_array_append_new(monitors, monitor);
    json_object_set_new(req, "clientRequestMonitorSettings", monitors);

    json_object_set_new(root, "sessionRequestData", req);

    char* dump = json_dumps(root, 0);
    std::string body(dump);
    free(dump);
    json_decref(root);
    return body;
}

int ParseSessionStatus(json_t* status)
{
    if (json_is_integer(status))
        return static_cast<int>(json_integer_value(status));

    if (!json_is_string(status))
        return -1;

    std::string value = json_string_value(status);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "queued")
        return 0;

    if (value == "provisioning" || value == "initializing" || value == "setup" ||
        value == "setting_up" || value == "launching" || value == "launching_game")
        return 1;

    if (value == "active" || value == "ready" || value == "paused")
        return 2;

    if (value == "streaming" || value == "playing" || value == "connected")
        return 3;

    if (value.find("ad") != std::string::npos)
        return 6;

    if (value.find("fail") != std::string::npos || value.find("error") != std::string::npos ||
        value.find("closed") != std::string::npos || value.find("terminated") != std::string::npos ||
        value.find("cancel") != std::string::npos)
        return 4;

    return -1;
}

} // namespace opennow::gfn::cloud_session
