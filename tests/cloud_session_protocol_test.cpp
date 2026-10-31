#include "gfn/cloud_session_protocol.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>

int main()
{
    using namespace opennow::gfn::cloud_session;
    constexpr auto browser=ClientProfile::BrowserWebRtc;
    constexpr auto native=ClientProfile::NativeWebRtc;
    const opennow::recovery::Identity identity {"user","provider","https://provider.example/","device","client"};
    auto* saved=json_object();
    opennow::gfn::cloud_session::StoreRoutingIdentity(saved,identity);
    const auto read=[&](const char* key) {return std::string(json_string_value(json_object_get(saved,key)));};
    const opennow::recovery::Identity stored {read("user_id"),read("provider_id"),read("provider_url"),read("device_id"),read("client_id")};
    assert(opennow::recovery::Matches(stored,identity)); // First ready poll, before streaming enrichment.
    auto changed=identity;changed.device="other-device";
    assert(!opennow::recovery::Matches(stored,changed));
    changed=identity;changed.client="other-client";
    assert(!opennow::recovery::Matches(stored,changed));
    bool incomplete=false;
    try {opennow::gfn::cloud_session::StoreRoutingIdentity(saved,{"user","provider","https://provider.example/","","client"});}
    catch(const std::runtime_error&) {incomplete=true;}
    assert(incomplete);
    json_decref(saved);
    opennow::StreamSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps = 60;
    settings.bitrate_kbps = 20000;
    settings.persist_game_settings = true;

    const std::string body = opennow::gfn::cloud_session::BuildSessionBody(
        "12345", "Test Game", "device-id", settings, "launch-context-one", browser);
    json_error_t error {};
    std::unique_ptr<json_t, decltype(&json_decref)> root(
        json_loads(body.c_str(), 0, &error), &json_decref);
    assert(root);
    assert(opennow::gfn::cloud_session::SubSessionIdFromRandom(std::vector<uint8_t>(16,0))=="00000000-0000-4000-8000-000000000000");
    assert(opennow::gfn::cloud_session::SubSessionIdFromRandom(std::vector<uint8_t>(16,1))=="01010101-0101-4101-8101-010101010101");
    bool bad_entropy=false;
    try {opennow::gfn::cloud_session::SubSessionIdFromRandom({1,2,3});} catch(const std::runtime_error&) {bad_entropy=true;}
    assert(bad_entropy);
    bool reused_device=false;
    try {opennow::gfn::cloud_session::BuildSessionBody("12345","Test","device-id",settings,"device-id",browser);}
    catch(const std::runtime_error&) {reused_device=true;}
    assert(reused_device);
    json_t* request = json_object_get(root.get(), "sessionRequestData");
    assert(request);
    assert(std::string(json_string_value(json_object_get(request,"deviceHashId")))=="device-id");
    auto* metadata=json_object_get(request,"metaData");
    bool has_sub_session=false;
    size_t mi;json_t* entry;
    json_array_foreach(metadata,mi,entry) {
        const auto* key=json_string_value(json_object_get(entry,"key"));
        if(key && std::string(key)=="SubSessionId") {
            has_sub_session=true;
            assert(std::string(json_string_value(json_object_get(entry,"value")))=="launch-context-one");
        }
    }
    assert(has_sub_session);
    assert(NewClientProfile("NVIDIA")==browser && NewClientProfile("ALLIANCE")==native);
    assert(ReadClientProfile(nullptr)==browser);
    auto* profile_record=json_object();assert(ReadClientProfile(profile_record)==native); // Legacy migration.
    StoreClientProfile(profile_record,browser);assert(ReadClientProfile(profile_record)==browser);
    auto* restored=json_deep_copy(profile_record);assert(ReadClientProfile(restored)==browser);json_decref(restored);
    StoreClientProfile(profile_record,native);assert(ReadClientProfile(profile_record)==native);
    json_object_set_new(profile_record,"cloudmatch_profile",json_string("future_unknown"));
    bool bad_profile=false;try {ReadClientProfile(profile_record);} catch(const std::runtime_error&) {bad_profile=true;}
    assert(bad_profile);json_decref(profile_record);
    const auto has=[](const auto& headers,const std::string& item) {return std::find(headers.begin(),headers.end(),item)!=headers.end();};
    for(const auto profile:{browser,native}) {
        const auto headers=BuildSessionHeaders("test-token","test-client","test-device",profile);
        assert(headers.front()=="Authorization: GFNJWT test-token"); // 401 refresh slot is invariant.
        assert(has(headers,"x-device-id: test-device") && has(headers,"nv-client-id: test-client"));
        assert(has(headers,profile==browser ? "nv-client-type: BROWSER" : "nv-client-type: NATIVE"));
        assert(has(headers,profile==browser ? "nv-client-streamer: WEBRTC" : "nv-client-streamer: NVIDIA-CLASSIC"));
    }
    bool bad_header=false;try {BuildSessionHeaders("token\r\nInjected: x","c","d",browser);} catch(const std::runtime_error&) {bad_header=true;}
    assert(bad_header);
    assert(std::string(json_string_value(json_object_get(request,"clientPlatformName")))=="browser");
    const auto legacy_body=BuildSessionBody("12345","Test Game","device-id",settings,"launch-context-one",native);
    auto* legacy=json_loads(legacy_body.c_str(),0,nullptr);
    assert(std::string(json_string_value(json_object_get(json_object_get(legacy,"sessionRequestData"),"clientPlatformName")))=="windows");
    auto* legacy_request=json_object_get(legacy,"sessionRequestData");
    assert(json_equal(json_object_get(request,"requestedStreamingFeatures"),json_object_get(legacy_request,"requestedStreamingFeatures")));
    assert(json_equal(json_object_get(request,"clientRequestMonitorSettings"),json_object_get(legacy_request,"clientRequestMonitorSettings")));
    assert(json_integer_value(json_object_get(legacy_request,"remoteControllersBitmap"))==0x0f0f);
    json_decref(legacy);

    assert(json_integer_value(json_object_get(request, "appId")) == 12345);
    assert(json_integer_value(json_object_get(request, "remoteControllersBitmap")) == 0x0f0f);
    json_t* monitors = json_object_get(request, "clientRequestMonitorSettings");
    assert(json_array_size(monitors) == 1);
    json_t* monitor = json_array_get(monitors, 0);
    assert(json_integer_value(json_object_get(monitor, "widthInPixels")) == 1920);

    json_t* queued = json_string("queued");
    json_t* streaming = json_string("STREAMING");
    json_t* failed = json_string("session_failed");
    assert(opennow::gfn::cloud_session::ParseSessionStatus(queued) == 0);
    assert(opennow::gfn::cloud_session::ParseSessionStatus(streaming) == 3);
    assert(opennow::gfn::cloud_session::ParseSessionStatus(failed) == 4);
    json_decref(queued);
    json_decref(streaming);
    json_decref(failed);
    const auto resume = opennow::gfn::cloud_session::BuildResumeBody("12345", "device-id", "sub-id", browser);
    std::unique_ptr<json_t, decltype(&json_decref)> resumed(
        json_loads(resume.c_str(), 0, nullptr), &json_decref);
    assert(resumed);
    assert(json_integer_value(json_object_get(resumed.get(), "action")) == 2);
    assert(std::string(json_string_value(json_object_get(resumed.get(), "data"))) == "RESUME");
    auto* minimal = json_object_get(resumed.get(), "sessionRequestData");
    assert(std::string(json_string_value(json_object_get(minimal,"clientPlatformName")))=="browser");
    assert(json_integer_value(json_object_get(minimal, "appId")) == 12345);
    assert(json_integer_value(json_object_get(minimal, "appLaunchMode")) == 2);
    assert(!json_object_get(minimal, "clientRequestMonitorSettings"));
    assert(!json_object_get(minimal, "clientStreamSettings"));
    assert(!json_object_get(minimal, "supportedVideoCodecs"));
    const auto native_resume=BuildResumeBody("12345","device-id","sub-id",native);
    auto* legacy_resume=json_loads(native_resume.c_str(),0,nullptr);
    assert(std::string(json_string_value(json_object_get(json_object_get(legacy_resume,"sessionRequestData"),"clientPlatformName")))=="windows");
    json_decref(legacy_resume);
    bool rejected = false;
    try { opennow::gfn::cloud_session::BuildResumeBody("123abc", "device-id", "sub-id", browser); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    const auto queue = [](const char* body) {
        std::unique_ptr<json_t, decltype(&json_decref)> response(json_loads(body, 0, nullptr), &json_decref);
        assert(response);
        opennow::SessionInfo info;
        opennow::gfn::cloud_session::ApplyQueueState(response.get(), info);
        return info;
    };
    auto info = queue(R"({"queuePosition":0,"session":{"queuePosition":121,"seatSetupInfo":{"queuePosition":126}}})");
    assert(info.queue_position == 126 && info.queue_source == "seat_setup" && info.queue_conflict);
    info = queue(R"({"queuePosition":126,"session":{"seatSetupInfo":{"queuePosition":0}}})");
    assert(info.queue_position == 0 && info.queue_conflict);
    info = queue(R"({"session":{"seatSetupInfo":{"seatSetupStep":1}}})");
    assert(info.seat_setup_step == 1 && info.queue_position == -1);
    info = queue(R"({"session":{"queuePosition":"17"}})");
    assert(info.queue_position == 17 && info.queue_source == "session");
    assert(queue(R"({"session":{"sessionProgress":{"queuePosition":15}}})").queue_position == 15);
    assert(queue(R"({"session":{"progressInfo":{"queuePosition":16}}})").queue_position == 16);
    assert(queue(R"({"queuePosition":21})").queue_position == 21);
    info = queue(R"({"session":{}})");
    assert(info.queue_position == -1 && info.queue_source == "missing");
    for (const auto* invalid : {"null", "-1", "2147483648", "true", "\"12x\"", "2.5"}) {
        const std::string body = std::string("{\"queuePosition\":99,\"session\":{\"seatSetupInfo\":{\"queuePosition\":") + invalid + "}}}";
        assert(queue(body.c_str()).queue_position == -1);
    }
    // An omitted position in a later full response must clear the old value.
    std::unique_ptr<json_t, decltype(&json_decref)> empty(json_loads("{}", 0, nullptr), &json_decref);
    info.queue_position = 126;
    opennow::gfn::cloud_session::ApplyQueueState(empty.get(), info);
    assert(info.queue_position == -1);
    return 0;
}
