#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace opennow::game_detail
{

inline bool IsUnknownMetadata(const std::string& value)
{
    if (value.empty())
        return true;

    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return normalized == "UNKNOWN" || normalized == "N/A" || normalized == "UNAVAILABLE";
}

inline std::string DisplayStore(const std::string& store)
{
    return IsUnknownMetadata(store) ? std::string("GeForce NOW") : store;
}

inline std::string DetailSubtitle(bool owned)
{
    return owned ? "In your GeForce NOW library" : "Available on GeForce NOW";
}

inline std::string HeaderSubtitle(bool owned)
{
    return owned ? "GeForce NOW library" : "GeForce NOW catalog";
}

inline std::string FormatLastPlayed(const std::string& value)
{
    if (value.empty())
        return "Never";

    if (value.size() >= 16 && value[4] == '-' && value[7] == '-' && value[10] == 'T' &&
        value[13] == ':' && value.back() == 'Z') {
        return value.substr(0, 10) + "  ·  " + value.substr(11, 5) + " UTC";
    }

    return value;
}

} // namespace opennow::game_detail
