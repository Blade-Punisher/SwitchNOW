#pragma once
#include "signaling_registration_policy.hpp"
#include <string>
#include <optional>
struct json_t;
namespace opennow::signaling {
// Every server envelope with a valid ackid requires an ACK, including our own identity.
std::optional<std::int64_t> IncomingAckId(json_t* envelope);
// Observe only existing registration evidence; does not emit protocol replies.
// Returns true exactly when this socket first has both identity and a matched ACK.
bool ObserveRegistration(RegistrationPolicy& state,json_t* envelope,const std::string& expected_name);
}
