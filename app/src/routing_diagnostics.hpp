#pragma once
#include "models.hpp"
#include <string>
#include <vector>
#include <utility>

namespace opennow::routing {
std::string Host(std::string value);
void Begin(const std::string& home, const std::string& mode, const std::string& provider);
void Event(const std::string& home, const std::string& event,
           const std::vector<std::pair<std::string, std::string>>& fields);
void Candidates(const std::string& home, const std::vector<StreamRegion>& regions,
                const std::string& source, const std::string& selected);
} // namespace opennow::routing
