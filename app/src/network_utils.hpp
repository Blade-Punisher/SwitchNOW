#pragma once

#include "network_quality_policy.hpp"
#include <string>

namespace opennow {

enum class NetworkConnectionType { Unknown, Wifi, Ethernet };

struct NetworkConnectionInfo {
    bool connected = true;
    NetworkConnectionType type = NetworkConnectionType::Unknown;
    network::WifiBand wifi_band = network::WifiBand::Unknown;
    int wifi_strength = -1;
};

class NetworkUtils {
public:
    static std::string GetLocalIPAddress();
    static NetworkConnectionInfo GetConnectionInfo();
    static bool HasInternetConnection();
};

} // namespace opennow
