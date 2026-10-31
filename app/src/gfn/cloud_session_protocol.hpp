#pragma once

#include "cloud_client_profile.hpp"
#include "../stream_settings.hpp"
#include "../models.hpp"
#include "../session_recovery_policy.hpp"

#include <jansson.h>

#include <string>
#include <vector>
#include <cstdint>

namespace opennow::gfn::cloud_session
{

std::string BuildSessionBody(
    const std::string& app_id,
    const std::string& internal_title,
    const std::string& device_id,
    const opennow::StreamSettings& stream_settings,
    const std::string& sub_session_id, ClientProfile profile);

std::string SubSessionIdFromRandom(const std::vector<uint8_t>& random);

int ParseSessionStatus(json_t* status);
void ApplyQueueState(json_t* response, SessionInfo& session);
// The initial saved record must contain the same IDs used by create/poll/resume.
void StoreRoutingIdentity(json_t* record, const opennow::recovery::Identity& identity);

std::string BuildResumeBody(const std::string& app_id, const std::string& device_id,
    const std::string& sub_session_id, ClientProfile profile);

} // namespace opennow::gfn::cloud_session
