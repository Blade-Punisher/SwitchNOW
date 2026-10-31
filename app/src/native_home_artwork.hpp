#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace opennow::shortcut {
bool UpdateInstalledHomeIcon(const std::string& nro_path, const std::vector<uint8_t>& jpeg);
}
