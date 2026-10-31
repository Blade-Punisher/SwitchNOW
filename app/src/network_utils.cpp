#ifdef __SWITCH__
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include "network_utils.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <net/if.h>
#include <net/if_media.h>
#include <sys/ioctl.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace opennow {

#ifdef __SWITCH__
namespace {

network::WifiBand WifiBandFromMediaWord(int media_word) {
    if (IFM_TYPE(media_word) != IFM_IEEE80211)
        return network::WifiBand::Unknown;
    switch (IFM_MODE(media_word)) {
        case IFM_IEEE80211_11B:
        case IFM_IEEE80211_11G:
        case IFM_IEEE80211_FH:
        case IFM_IEEE80211_11NG:
        case IFM_IEEE80211_VHT2G:
            return network::WifiBand::Ghz2_4;
        case IFM_IEEE80211_11A:
        case IFM_IEEE80211_11NA:
        case IFM_IEEE80211_VHT5G:
            return network::WifiBand::Ghz5;
        default:
            return network::WifiBand::Unknown;
    }
}

network::WifiBand DetectWifiBandFromInterfaces() {
    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return network::WifiBand::Unknown;
    std::array<char, 2048> interfaces {};
    ifconf config {};
    config.ifc_len = static_cast<int>(interfaces.size());
    config.ifc_buf = interfaces.data();
    if (ioctl(sock, SIOCGIFCONF, &config) < 0) {
        close(sock);
        return network::WifiBand::Unknown;
    }
    const char* cursor = interfaces.data();
    const char* const end = cursor + config.ifc_len;
    while (cursor + sizeof(ifreq) <= end) {
        const auto* request = reinterpret_cast<const ifreq*>(cursor);
        if (request->ifr_addr.sa_family == AF_INET &&
            std::strncmp(request->ifr_name, "lo", 2) != 0) {
            ifmediareq media {};
            std::memcpy(media.ifm_name, request->ifr_name, sizeof(media.ifm_name));
            media.ifm_name[sizeof(media.ifm_name) - 1] = '\0';
            if (ioctl(sock, SIOCGIFMEDIA, &media) == 0) {
                const int word = (media.ifm_status & IFM_ACTIVE)
                    ? media.ifm_active : media.ifm_current;
                const auto band = WifiBandFromMediaWord(word);
                if (band != network::WifiBand::Unknown) {
                    close(sock);
                    return band;
                }
            }
        }
        cursor += _SIZEOF_ADDR_IFREQ(*request);
    }
    close(sock);
    return network::WifiBand::Unknown;
}

network::WifiBand DetectLegacyWifiBand() {
    if (!hosversionBefore(15, 0, 0) || R_FAILED(wlaninfInitialize()))
        return network::WifiBand::Unknown;
    struct LegacyConnectionStatus {
        u32 state;
        u32 unknown;
        u16 channel;
        u8 remaining[0x32];
    };
    static_assert(sizeof(LegacyConnectionStatus) == 0x3c);
    LegacyConnectionStatus status {};
    const Result rc = serviceDispatchOut(wlaninfGetServiceSession(), 9, status);
    wlaninfExit();
    return R_SUCCEEDED(rc)
        ? network::WifiBandFromChannel(status.channel)
        : network::WifiBand::Unknown;
}

} // namespace
#endif

std::string NetworkUtils::GetLocalIPAddress() {
    std::string local_ip = "127.0.0.1";

#ifdef _WIN32
    // Windows implementation (fallback if compiled for PC)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock != -1) {
        struct sockaddr_in serv;
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = inet_addr("8.8.8.8");
        serv.sin_port = htons(53);

        if (connect(sock, (const struct sockaddr*)&serv, sizeof(serv)) == 0) {
            struct sockaddr_in name;
            socklen_t namelen = sizeof(name);
            if (getsockname(sock, (struct sockaddr*)&name, &namelen) == 0) {
                local_ip = inet_ntoa(name.sin_addr);
            }
        }
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return local_ip;
}

bool NetworkUtils::HasInternetConnection() {
#ifdef __SWITCH__
    NifmInternetConnectionType type {};
    NifmInternetConnectionStatus status {};
    u32 strength = 0;
    const Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    if (R_FAILED(rc))
        return true; // Unknown service state must not terminate a healthy stream.
    return status == NifmInternetConnectionStatus_Connected;
#else
    return true;
#endif
}

NetworkConnectionInfo NetworkUtils::GetConnectionInfo() {
    NetworkConnectionInfo info;
#ifdef __SWITCH__
    NifmInternetConnectionType type {};
    NifmInternetConnectionStatus status {};
    u32 strength = 0;
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
        return info;
    info.connected = status == NifmInternetConnectionStatus_Connected;
    info.wifi_strength = static_cast<int>(std::min<u32>(strength, 3));
    if (type == NifmInternetConnectionType_WiFi) {
        info.type = NetworkConnectionType::Wifi;
        if (info.connected) {
            info.wifi_band = DetectLegacyWifiBand();
            if (info.wifi_band == network::WifiBand::Unknown)
                info.wifi_band = DetectWifiBandFromInterfaces();
        }
    } else if (type == NifmInternetConnectionType_Ethernet) {
        info.type = NetworkConnectionType::Ethernet;
        info.wifi_strength = -1;
    }
#endif
    return info;
}

} // namespace opennow
