#pragma once
#include "server_location_policy.hpp"
#include <string>

namespace opennow::routing {
struct Assignment {
    std::string session_id, requested_host, initial_host, actual_host;
    std::string requested_url;
    unsigned redirects = 0;
    bool Observe(const std::string& id, const std::string& requested, const std::string& actual) {
        if (session_id != id) { *this = {}; session_id = id; }
        if (!requested.empty() && requested_host.empty()) {
            requested_host = server_location::RouteHost(requested); requested_url = requested;
        }
        const auto host = server_location::RouteHost(actual);
        if (host.empty()) return false;
        if (initial_host.empty()) initial_host = host;
        const bool changed = !actual_host.empty() && actual_host != host;
        if (changed) ++redirects;
        actual_host = host;
        return changed;
    }
};
inline std::string ShortHost(const std::string& endpoint) {
    auto host = server_location::RouteHost(endpoint);
    const std::string suffix = ".cloudmatchbeta.nvidiagrid.net";
    if (host.ends_with(suffix)) host.resize(host.size() - suffix.size());
    if (host.size() > 24) host = host.substr(0, 21) + "...";
    return host.empty() ? "--" : host;
}
}
