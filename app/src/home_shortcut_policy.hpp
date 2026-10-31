#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace opennow::shortcut
{

struct LaunchRequest
{
    std::string launch_app_id;
    std::string game_id;
    std::string title;
    std::string store;
    std::string image_url;
    std::string executable_path;
    // Runtime provenance only; never trusted from manifest contents.
    std::string manifest_path;
};

inline bool IsNumericLaunchId(std::string_view value)
{
    return !value.empty() &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
}

inline int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F')
        return 10 + ch - 'A';
    return -1;
}

inline std::string EncodeValue(std::string_view value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char ch : value)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '/' || ch == ':')
        {
            encoded.push_back(static_cast<char>(ch));
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(kHex[ch >> 4]);
            encoded.push_back(kHex[ch & 0x0F]);
        }
    }
    return encoded;
}

inline std::optional<std::string> DecodeValue(std::string_view value)
{
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] != '%')
        {
            decoded.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size())
            return std::nullopt;
        const int high = HexValue(value[i + 1]);
        const int low  = HexValue(value[i + 2]);
        if (high < 0 || low < 0)
            return std::nullopt;
        decoded.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }
    return decoded;
}

inline bool IsValid(const LaunchRequest& request)
{
    return IsNumericLaunchId(request.launch_app_id) &&
        request.launch_app_id.size() <= 32 &&
        !request.title.empty() && request.title.size() <= 511 &&
        request.game_id.size() <= 512 && request.store.size() <= 256 &&
        request.image_url.size() <= 2048 &&
        request.executable_path.size() <= 768;
}

inline std::string Serialize(const LaunchRequest& request)
{
    std::ostringstream output;
    output << "SWITCHNOW_SHORTCUT_V1\n"
           << "nro_path=" << EncodeValue(request.executable_path) << '\n'
           << "launch_app_id=" << EncodeValue(request.launch_app_id) << '\n'
           << "game_id=" << EncodeValue(request.game_id) << '\n'
           << "title=" << EncodeValue(request.title) << '\n'
           << "store=" << EncodeValue(request.store) << '\n'
           << "image_url=" << EncodeValue(request.image_url) << '\n';
    return output.str();
}

inline std::optional<LaunchRequest> Parse(std::string_view manifest)
{
    if (manifest.size() > 16 * 1024)
        return std::nullopt;

    std::istringstream input {std::string(manifest)};
    std::string line;
    if (!std::getline(input, line) || line != "SWITCHNOW_SHORTCUT_V1")
        return std::nullopt;

    LaunchRequest request;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string key = line.substr(0, separator);
        const auto value = DecodeValue(std::string_view(line).substr(separator + 1));
        if (!value)
            return std::nullopt;
        if (key == "nro_path")
            request.executable_path = *value;
        else if (key == "launch_app_id")
            request.launch_app_id = *value;
        else if (key == "game_id")
            request.game_id = *value;
        else if (key == "title")
            request.title = *value;
        else if (key == "store")
            request.store = *value;
        else if (key == "image_url")
            request.image_url = *value;
    }

    return IsValid(request) ? std::optional<LaunchRequest>(std::move(request))
                            : std::nullopt;
}

inline std::optional<LaunchRequest> ParseArguments(
    const std::vector<std::string>& arguments)
{
    LaunchRequest request;
    bool saw_launch_argument = false;
    for (const std::string& argument : arguments)
    {
        const size_t separator = argument.find('=');
        if (separator == std::string::npos)
            continue;
        const std::string_view key(argument.data(), separator);
        const std::string value = argument.substr(separator + 1);
        if (key == "--launch-app-id")
        {
            request.launch_app_id = value;
            saw_launch_argument = true;
        }
        else if (key == "--game-id")
            request.game_id = value;
        else if (key == "--title")
            request.title = value;
        else if (key == "--store")
            request.store = value;
        else if (key == "--image-url")
            request.image_url = value;
    }
    if (request.title.empty() && saw_launch_argument)
        request.title = "GeForce NOW game";
    return saw_launch_argument && IsValid(request)
        ? std::optional<LaunchRequest>(std::move(request))
        : std::nullopt;
}

inline std::string SafeFileStem(std::string_view title, std::string_view fallback)
{
    std::string stem;
    stem.reserve(std::min<size_t>(title.size(), 64));
    bool last_was_space = false;
    for (size_t index = 0; index < title.size();)
    {
        const unsigned char ch =
            static_cast<unsigned char>(title[index]);
        if (ch >= 0x80)
        {
            size_t sequence_size = 1;
            if ((ch & 0xE0) == 0xC0)
                sequence_size = 2;
            else if ((ch & 0xF0) == 0xE0)
                sequence_size = 3;
            else if ((ch & 0xF8) == 0xF0)
                sequence_size = 4;
            if (index + sequence_size > title.size() ||
                stem.size() + sequence_size > 64)
                break;
            bool valid_sequence = sequence_size > 1;
            for (size_t byte = 1; byte < sequence_size; ++byte)
            {
                if ((static_cast<unsigned char>(title[index + byte]) & 0xC0) !=
                    0x80)
                    valid_sequence = false;
            }
            if (!valid_sequence)
            {
                ++index;
                continue;
            }
            stem.append(title.substr(index, sequence_size));
            index += sequence_size;
            last_was_space = false;
            continue;
        }

        ++index;
        if (stem.size() >= 64)
            break;
        const bool forbidden = ch < 32 || ch == '<' || ch == '>' || ch == ':' ||
            ch == '"' || ch == '/' || ch == '\\' || ch == '|' || ch == '?' ||
            ch == '*';
        if (forbidden)
            continue;
        if (std::isspace(ch))
        {
            if (!stem.empty() && !last_was_space)
                stem.push_back(' ');
            last_was_space = true;
        }
        else
        {
            stem.push_back(static_cast<char>(ch));
            last_was_space = false;
        }
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
        stem.pop_back();
    return stem.empty() ? std::string(fallback) : stem;
}

} // namespace opennow::shortcut
