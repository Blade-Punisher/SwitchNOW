#pragma once
#include <string>
#include <vector>
#include <jansson.h>

namespace opennow::gfn::endpoints {
struct Selection {
    std::string control_host;
    std::string signaling_url;
    std::string control_source = "missing";
    std::string signaling_source = "missing";
    std::vector<std::string> signaling_candidates;
    std::vector<std::string> descriptor_summaries; // Bounded numeric/protocol metadata, no raw URLs.
    unsigned usage14_count = 0, usage16_count = 0;
};
// Media usage/ports are intentionally never used to construct signaling.
Selection Resolve(json_t* session);
// Prefer fresh authoritative data; only advance if the preferred URL already failed.
std::string RecoveryEndpoint(const std::string& preferred, const std::vector<std::string>& candidates,
    const std::string& failed);
// Validated HTTPS control base; accepts an authority hint and normalizes default port.
std::string ControlBaseUrl(const std::string& value);
// Preserve server query fields; override only client-owned registration fields.
std::string SignInUrl(const std::string& base, const std::string& peer, const std::string& session_id, bool query_registration);
bool ValidSignalingUrl(const std::string& url);
// Only a host and a fixed known route are exposed; query/userinfo are excluded.
std::string SafeRoute(const std::string& url);
}
