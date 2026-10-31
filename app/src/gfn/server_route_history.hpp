#pragma once
#include "../server_route_policy.hpp"

namespace opennow::server_route {
std::vector<Observation> ParseHistory(const std::string& text);
std::string SerializeHistory(const std::vector<Observation>& rows);
void Remember(std::vector<Observation>& rows, Observation current);
} // namespace opennow::server_route
