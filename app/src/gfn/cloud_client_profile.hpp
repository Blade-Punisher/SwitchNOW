#pragma once
#include <string>
#include <vector>
struct json_t;
namespace opennow::gfn::cloud_session {
enum class ClientProfile { NativeWebRtc, BrowserWebRtc };
ClientProfile NewClientProfile(const std::string& provider_code);
const char* ProfileName(ClientProfile profile);
ClientProfile ReadClientProfile(json_t* record);
void StoreClientProfile(json_t* record, ClientProfile profile);
std::vector<std::string> BuildSessionHeaders(const std::string& token, const std::string& client_id,
    const std::string& device_id, ClientProfile profile);
}
