#include "gfn_client.hpp"
#include "server_probe_policy.hpp"
#include "network_resource_policy.hpp"
#include "session_recovery_policy.hpp"
#include "ad_break_policy.hpp"
#include "cloud_session_policy.hpp"
#include "proxy_policy.hpp"
#include "device_identity.hpp"
#include "play_history.hpp"
#include "server_location_policy.hpp"
#include "server_assignment_policy.hpp"
#include "routing_diagnostics.hpp"
#include "gfn/app_launch_mode_policy.hpp"
#include "gfn/cloud_session_protocol.hpp"
#include "gfn/session_endpoints.hpp"
#include "gfn/session_cleanup.hpp"
#include "gfn/library_catalog.hpp"
#include "gfn/game_feed_cache.hpp"
#include "gfn/library_ownership.hpp"
#include "game_feed_policy.hpp"
#include "game_feed_models.hpp"
#include "gfn/server_route_history.hpp"
#include "gfn/server_zones.hpp"
#include "server_zone_policy.hpp"
#include "membership_tier_policy.hpp"
#include "app_version.hpp"

#ifdef __SWITCH__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <switch.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <jansson.h>
#include <curl/curl.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>

#include "network_utils.hpp"
#include "auth_policy.hpp"
#include "native_auth_policy.hpp"
#include "runtime_journal.hpp"
#include "library_metadata_policy.hpp"
#include "stream_settings.hpp"
#include "stream_diagnostics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace opennow
{
std::vector<AuthSession> LoadAccountsFromDisk(std::string* active_user_id);
void SaveAccountsToDisk(const std::vector<AuthSession>& sessions, const std::string& active_user_id);

namespace
{

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

constexpr const char* kServiceUrlsEndpoint = "https://pcs.geforcenow.com/v1/serviceUrls";
constexpr const char* kPublicCatalogEndpoint =
    "https://static.nvidiagrid.net/supported-public-game-list/locales/gfnpc-en-US.json";
constexpr const char* kTokenEndpoint       = "https://login.nvidia.com/token";
constexpr const char* kClientTokenEndpoint = "https://login.nvidia.com/client_token";
constexpr const char* kUserInfoEndpoint    = "https://login.nvidia.com/userinfo";
constexpr const char* kSubscriptionEndpoint = "https://mes.geforcenow.com/v4/subscriptions";
constexpr const char* kAuthorizeEndpoint   = "https://login.nvidia.com/authorize";
constexpr const char* kGraphQlEndpoint     = "https://games.geforce.com/graphql";

constexpr const char* kClientId          = "ZU7sPN-miLujMD95LfOQ453IB0AtjM8sMyvgJ9wCXEQ";
constexpr const char* kClientVersion     = "2.0.80.173";
constexpr const char* kLcarsClientId     = "ec7e38d4-03af-4b58-b131-cfb0495903ab";
constexpr const char* kScopes            = "openid consent email tk_client age";
constexpr const char* kDefaultLocale     = "en_US";
constexpr const char* kDefaultProviderId = "PDiAhv2kJTFeQ7WOPqiQ2tRZ7lGhR2X11dXvM4TZSxg";
constexpr const char* kRedirectUri       = "http://localhost:2259";
constexpr int kLoginCallbackPort         = 2259;

constexpr const char* kPanelsQueryHash          = "f8e26265a5db5c20e1334a6872cf04b6e3970507697f6ae55a6ddefa5420daf0";
constexpr const char* kLibraryWithTimeQueryHash = "039e8c0d553972975485fee56e59f2549d2fdb518e247a42ab5022056a74406f";
constexpr const char* kAppMetadataQueryHash     = "39187e85b6dcf60b7279a5f233288b0a8b69a8b1dbcfb5b25555afdcb988f0d7";

constexpr const char* kNvidiaFileOrigin  = "https://nvfile";
constexpr const char* kNvidiaFileReferer = "https://nvfile/";
constexpr const char* kPlayOrigin        = "https://play.geforcenow.com";
constexpr const char* kPlayReferer       = "https://play.geforcenow.com/";

constexpr std::int64_t kRefreshWindowMs = 10LL * 60LL * 1000LL;
constexpr std::int64_t kMembershipRefreshIntervalMs = 60LL * 60LL * 1000LL;
constexpr std::int64_t kMembershipRetryIntervalMs = 5LL * 60LL * 1000LL;
std::recursive_mutex g_accounts_mutex;
std::recursive_mutex g_active_cloud_session_mutex;
std::mutex g_reauthentication_mutex;

struct RegionSelectionCache
{
    std::mutex mutex;
    std::string provider_url;
    std::string selected_url;
    std::string selected_name;
    std::vector<StreamRegion> regions;
    std::chrono::steady_clock::time_point measured_at {};
    bool provider_fallback = false;
};

RegionSelectionCache g_region_selection_cache;
constexpr auto kRegionSelectionCacheLifetime = std::chrono::seconds(60);
constexpr auto kProviderFallbackCacheLifetime = std::chrono::minutes(5);
constexpr std::int64_t kPersistentRegionCacheLifetimeMs =
    60LL * 1000LL;
constexpr std::int64_t kRegionDisplayCacheLifetimeMs =
    7LL * 24LL * 60LL * 60LL * 1000LL;

bool ZoneCatalogEligible(const AuthSession& session) {
    return session.provider.code == "NVIDIA" && server_zone::IsOfficialProvider(session.provider.streaming_service_url) &&
        membership::IsVerifiedFreeTier(session.user.membership_tier, session.user.membership_tier_verified);
}

void FilterZoneCatalog(std::vector<StreamRegion>& regions, bool eligible) {
    if (!eligible) std::erase_if(regions, [](const auto& r) { return !r.zone_id.empty(); });
}

std::string Trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(begin, end - begin);
}

std::int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string GetAppHome()
{
    return "sdmc:/switch/SwitchNOW";
}

std::string GetSessionPath()
{
    return GetAppHome() + "/auth_session.json";
}

std::string GetAccountsPath()
{
    return GetAppHome() + "/auth_accounts.json";
}

std::string GetDeviceIdPath()
{
    return GetAppHome() + "/device_id.txt";
}

std::string GetLauncherPreferencesPath()
{
    return GetAppHome() + "/launcher_preferences.json";
}

std::string GetActiveCloudSessionPath()
{
    return GetAppHome() + "/active_cloud_session.json";
}

std::string GetNativeCredentialsPath()
{
    return GetAppHome() + "/auth_credentials.vault";
}

std::string GetRegionSelectionCachePath()
{
    return GetAppHome() + "/server_region_cache.json";
}

void TrackSessionAssignment(SessionInfo& info) {
    if (info.session_id.empty()) return;
    static std::mutex assignment_mutex;
    static std::deque<routing::Assignment> assignments;
    routing::Assignment snapshot;
    std::string previous;
    bool changed;
    {
        std::lock_guard<std::mutex> lock(assignment_mutex);
        auto found = std::find_if(assignments.begin(), assignments.end(), [&](const auto& item) {
            return item.session_id == info.session_id;
        });
        if (found == assignments.end()) {
            if (assignments.size() >= 8) assignments.pop_front();
            assignments.emplace_back(); found = std::prev(assignments.end());
        }
        previous = found->actual_host;
        changed = found->Observe(info.session_id, info.requested_region_url, info.server_ip);
        snapshot = *found;
    }
    info.initial_server_ip = snapshot.initial_host;
    if (!snapshot.requested_url.empty()) info.requested_region_url = snapshot.requested_url;
    info.server_redirects = snapshot.redirects;
    if (changed) routing::Event(GetAppHome(), "redirect", {
        {"requested_host", snapshot.requested_host}, {"initial_host", snapshot.initial_host},
        {"previous_host", previous}, {"actual_host", snapshot.actual_host},
        {"changes", std::to_string(snapshot.redirects)}, {"state", std::to_string(info.status)}});
}

void EnsureAppHome()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/SwitchNOW", 0777);
#endif
}

JsonPtr LoadJson(const std::string& body)
{
    json_error_t error {};
    JsonPtr root(json_loads(body.c_str(), 0, &error), &json_decref);
    if (!root)
    {
        throw std::runtime_error(
            "JSON parse failed at line " + std::to_string(error.line) + ": " + error.text);
    }

    return root;
}

std::string JsonString(json_t* value)
{
    if (json_is_string(value))
    {
        const char* raw = json_string_value(value);
        return raw ? raw : "";
    }

    if (json_is_integer(value))
        return std::to_string(json_integer_value(value));

    return "";
}

std::string GetString(json_t* object, const char* key)
{
    if (!object || !json_is_object(object))
        return "";

    return JsonString(json_object_get(object, key));
}

bool GetBool(json_t* object, const char* key, bool fallback = false)
{
    if (!object || !json_is_object(object))
        return fallback;

    json_t* value = json_object_get(object, key);
    if (json_is_boolean(value))
        return json_boolean_value(value);

    return fallback;
}

int GetInteger(json_t* object, const char* key, int fallback = 0)
{
    if (!object || !json_is_object(object))
        return fallback;

    json_t* value = json_object_get(object, key);
    if (!json_is_integer(value))
        return fallback;

    return static_cast<int>(json_integer_value(value));
}

double JsonNumber(json_t* object, const char* key, double fallback = 0.0)
{
    if (!object || !json_is_object(object))
        return fallback;

    json_t* value = json_object_get(object, key);
    if (json_is_integer(value))
        return static_cast<double>(json_integer_value(value));
    if (json_is_real(value))
        return json_real_value(value);
    if (!json_is_string(value))
        return fallback;

    const char* text = json_string_value(value);
    if (!text || !*text)
        return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    return end != text && std::isfinite(parsed) ? parsed : fallback;
}

std::string AddonAttribute(json_t* addon, const char* key)
{
    json_t* attributes = addon ? json_object_get(addon, "attributes") : nullptr;
    if (!json_is_array(attributes))
        return {};

    size_t index;
    json_t* attribute;
    json_array_foreach(attributes, index, attribute)
    {
        if (GetString(attribute, "key") == key)
            return GetString(attribute, "textValue");
    }
    return {};
}

bool ParseNumberText(const std::string& text, double& output)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed))
        return false;
    output = parsed;
    return true;
}

SubscriptionInfo ParseSubscription(json_t* root)
{
    SubscriptionInfo info;
    info.available = true;
    info.membership_tier = Trim(GetString(root, "membershipTier"));
    info.subscription_type = GetString(root, "type");
    info.subscription_subtype = GetString(root, "subType");

    const double allotted_minutes = JsonNumber(root, "allottedTimeInMinutes");
    const double purchased_minutes = JsonNumber(root, "purchasedTimeInMinutes");
    const double rolled_over_minutes = JsonNumber(root, "rolledOverTimeInMinutes");
    const double fallback_total = allotted_minutes + purchased_minutes + rolled_over_minutes;
    const double total_minutes = JsonNumber(root, "totalTimeInMinutes", fallback_total);
    const double remaining_minutes = JsonNumber(root, "remainingTimeInMinutes");

    info.allotted_hours = allotted_minutes / 60.0;
    info.purchased_hours = purchased_minutes / 60.0;
    info.rolled_over_hours = rolled_over_minutes / 60.0;
    info.total_hours = total_minutes / 60.0;
    info.remaining_hours = remaining_minutes / 60.0;
    info.used_hours = std::max(total_minutes - remaining_minutes, 0.0) / 60.0;
    info.is_unlimited = info.subscription_subtype == "UNLIMITED";

    json_t* addons = root ? json_object_get(root, "addons") : nullptr;
    if (!json_is_array(addons))
        return info;

    size_t index;
    json_t* addon;
    json_array_foreach(addons, index, addon)
    {
        if (GetString(addon, "type") != "STORAGE" ||
            GetString(addon, "subType") != "PERMANENT_STORAGE" ||
            GetString(addon, "status") != "OK")
        {
            continue;
        }

        info.has_storage = true;
        const bool has_size = ParseNumberText(
            AddonAttribute(addon, "TOTAL_STORAGE_SIZE_IN_GB"), info.storage_size_gb);
        const bool has_used = ParseNumberText(
            AddonAttribute(addon, "USED_STORAGE_SIZE_IN_GB"), info.storage_used_gb);
        info.has_storage_usage = has_size && has_used;
        info.storage_region_name = AddonAttribute(addon, "STORAGE_METRO_REGION_NAME");
        info.storage_region_code = AddonAttribute(addon, "STORAGE_METRO_REGION");
        break;
    }
    return info;
}

bool HasPrefix(const std::string& value, const char* prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string ExtractHostFromUrlLike(const std::string& value)
{
    size_t start = value.find("://");
    if (start == std::string::npos)
        return "";

    start += 3;
    const size_t path = value.find('/', start);
    std::string host_port = value.substr(start, path == std::string::npos ? std::string::npos : path - start);

    if (!host_port.empty() && host_port.front() == '[') {
        const size_t close = host_port.find(']');
        return close == std::string::npos ? "" : host_port.substr(1, close - 1);
    }

    const size_t colon = host_port.find(':');
    return colon == std::string::npos ? host_port : host_port.substr(0, colon);
}

int ExtractPortFromUrlLike(const std::string& value)
{
    size_t start = value.find("://");
    if (start == std::string::npos)
        return 0;

    start += 3;
    const size_t path = value.find('/', start);
    const size_t host_end = path == std::string::npos ? value.size() : path;

    size_t colon = std::string::npos;
    if (start < value.size() && value[start] == '[') {
        const size_t close = value.find(']', start);
        if (close != std::string::npos && close + 1 < host_end && value[close + 1] == ':')
            colon = close + 1;
    } else {
        colon = value.find(':', start);
        if (colon >= host_end)
            colon = std::string::npos;
    }

    if (colon == std::string::npos || colon + 1 >= host_end)
        return 0;

    char* end = nullptr;
    long port = std::strtol(value.c_str() + colon + 1, &end, 10);
    if (port <= 0 || port > 65535)
        return 0;

    return static_cast<int>(port);
}

std::string GetConnectionIp(json_t* connection, const std::string& fallback_ip = "")
{
    if (!connection || !json_is_object(connection))
        return fallback_ip;

    json_t* ip = json_object_get(connection, "ip");
    if (json_is_string(ip))
        return JsonString(ip);

    if (json_is_array(ip) && json_array_size(ip) > 0)
        return JsonString(json_array_get(ip, 0));

    const std::string resource_path = GetString(connection, "resourcePath");
    const std::string host = ExtractHostFromUrlLike(resource_path);
    return host.empty() ? fallback_ip : host;
}

int GetConnectionPort(json_t* connection)
{
    const int direct_port = GetInteger(connection, "port");
    if (direct_port > 0)
        return direct_port;

    return ExtractPortFromUrlLike(GetString(connection, "resourcePath"));
}


void AppendIceServer(SessionInfo& info, std::string url, std::string username, std::string credential)
{
    url = Trim(url);
    if (url.empty())
        return;

    const bool is_ice_url =
        HasPrefix(url, "stun:") || HasPrefix(url, "stuns:") ||
        HasPrefix(url, "turn:") || HasPrefix(url, "turns:");
    if (!is_ice_url)
        return;

    const auto duplicate = std::find_if(
        info.ice_servers.begin(),
        info.ice_servers.end(),
        [&url](const IceServerInfo& existing) {
            return existing.url == url;
        });
    if (duplicate != info.ice_servers.end())
        return;

    IceServerInfo ice;
    ice.url        = std::move(url);
    ice.username   = std::move(username);
    ice.credential = std::move(credential);
    info.ice_servers.push_back(std::move(ice));
}

void CollectIceServerEntry(json_t* value, SessionInfo& info, const std::string& username = {}, const std::string& credential = {})
{
    if (!value)
        return;

    if (json_is_string(value))
    {
        AppendIceServer(info, JsonString(value), username, credential);
        return;
    }

    if (json_is_array(value))
    {
        size_t index;
        json_t* entry;
        json_array_foreach(value, index, entry)
            CollectIceServerEntry(entry, info, username, credential);
        return;
    }

    if (!json_is_object(value))
        return;

    const std::string entry_username =
        GetString(value, "username").empty() ? username : GetString(value, "username");
    std::string entry_credential = GetString(value, "credential");
    if (entry_credential.empty())
        entry_credential = GetString(value, "password");
    if (entry_credential.empty())
        entry_credential = credential;

    const char* url_keys[] = {
        "urls", "url", "uri", "server", "serverUrl", "stunUrl", "turnUrl"
    };

    for (const char* key : url_keys)
        CollectIceServerEntry(json_object_get(value, key), info, entry_username, entry_credential);
}

void CollectIceServersFromObject(json_t* object, SessionInfo& info)
{
    if (!object || !json_is_object(object))
        return;

    const char* nested_ice_config_keys[] = {
        "iceServerConfiguration",
        "iceConfiguration",
        "rtcConfiguration",
        "webrtcConfiguration",
    };

    for (const char* key : nested_ice_config_keys)
        CollectIceServersFromObject(json_object_get(object, key), info);

    const char* ice_keys[] = {
        "iceServers",
        "ice_servers",
        "iceServer",
        "ice_server",
        "rtcIceServers",
        "webrtcIceServers",
        "stunServers",
        "turnServers",
    };

    for (const char* key : ice_keys)
        CollectIceServerEntry(json_object_get(object, key), info);
}

void AppendSessionTraceLog(const std::string& line);

void ApplySessionNetworkInfo(json_t* sess, SessionInfo& info)
{
    if (!sess || !json_is_object(sess))
        return;

    CollectIceServersFromObject(sess, info);

    const std::string session_token = GetString(sess, "sessionToken");
    if (!session_token.empty())
        info.session_token = session_token;

    const std::string server_ip = GetString(sess, "serverIp");
    if (!server_ip.empty())
        info.server_ip = server_ip;

    json_t* session_control = json_object_get(sess, "sessionControlInfo");
    CollectIceServersFromObject(session_control, info);
    if (info.server_ip.empty() && session_control)
        info.server_ip = GetString(session_control, "ip");

    const auto endpoints = gfn::endpoints::Resolve(sess);
    info.control_ip = endpoints.control_host;
    info.signaling_url = endpoints.signaling_url;
    info.signaling_candidates = endpoints.signaling_candidates;
    if (info.status == 2 || info.status == 3 || info.status == 6)
        AppendSessionTraceLog("ROUTING event=endpoint_resolution control_host=" + routing::Host(info.control_ip) +
            " control_source=" + endpoints.control_source +
            " signaling_route=" + gfn::endpoints::SafeRoute(info.signaling_url) +
            " signaling_source=" + endpoints.signaling_source +
            " usage14=" + std::to_string(endpoints.usage14_count) +
            " usage16=" + std::to_string(endpoints.usage16_count) +
            " choices=" + std::to_string(endpoints.signaling_candidates.size()) +
            " profile=" + gfn::cloud_session::ProfileName(info.cloud_client_profile));
    if (info.status == 2 || info.status == 3 || info.status == 6) {
        for (const auto& descriptor : endpoints.descriptor_summaries)
            AppendSessionTraceLog("ROUTING event=signaling_descriptor " + descriptor);
        for (size_t candidate=0;candidate<endpoints.signaling_candidates.size();++candidate)
            AppendSessionTraceLog("ROUTING event=signaling_candidate index=" + std::to_string(candidate) +
                " route=" + gfn::endpoints::SafeRoute(endpoints.signaling_candidates[candidate]));
    }

    json_t* connection_info_array = json_object_get(sess, "connectionInfo");
    if (connection_info_array && json_is_array(connection_info_array)) {
        const int priorities[] = {2, 17, 14};
        for (int priority : priorities) {
            std::string best_ip;
            int best_port = 0;

            size_t index;
            json_t* conn_info;
            json_array_foreach(connection_info_array, index, conn_info) {
                CollectIceServersFromObject(conn_info, info);

                if (GetInteger(conn_info, "usage") != priority)
                    continue;

                const std::string fallback_ip = priority == 14 ? info.server_ip : "";
                const std::string ip = GetConnectionIp(conn_info, fallback_ip);
                const int port = GetConnectionPort(conn_info);
                if (!ip.empty() && port > 0 && (priority != 14 || port > best_port)) {
                    best_ip = ip;
                    best_port = port;
                }
            }

            if (!best_ip.empty() && best_port > 0) {
                info.media_ip = best_ip;
                info.media_port = best_port;
                break;
            }
        }

    }
}

std::string EnsureTrailingSlash(std::string value)
{
    if (value.empty() || value.back() == '/')
        return value;

    value.push_back('/');
    return value;
}

LoginProvider DefaultProvider()
{
    LoginProvider provider;
    provider.idp_id                = kDefaultProviderId;
    provider.code                  = "NVIDIA";
    provider.display_name          = "NVIDIA";
    provider.streaming_service_url = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    provider.priority              = 0;
    return provider;
}

std::vector<unsigned char> GenerateRandomBytes(size_t length)
{
    std::vector<unsigned char> bytes(length);

#ifdef __SWITCH__
    randomGet(bytes.data(), bytes.size());
#else
    std::random_device device;
    for (auto& byte : bytes)
        byte = static_cast<unsigned char>(device());
#endif

    return bytes;
}

std::string HexEncode(const unsigned char* data, size_t length)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string output;
    output.reserve(length * 2);

    for (size_t index = 0; index < length; ++index)
    {
        output.push_back(kDigits[(data[index] >> 4) & 0x0F]);
        output.push_back(kDigits[data[index] & 0x0F]);
    }

    return output;
}

std::vector<unsigned char> HexDecode(const std::string& input)
{
    if ((input.size() & 1U) != 0)
        throw std::runtime_error("Invalid vault hex length");

    auto value = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };

    std::vector<unsigned char> output(input.size() / 2);
    for (size_t i = 0; i < output.size(); ++i)
    {
        const int high = value(input[i * 2]);
        const int low = value(input[i * 2 + 1]);
        if (high < 0 || low < 0)
            throw std::runtime_error("Invalid vault hex data");
        output[i] = static_cast<unsigned char>((high << 4) | low);
    }
    return output;
}

std::string Base64UrlEncode(const unsigned char* data, size_t length)
{
    static constexpr char kBase64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((length + 2) / 3) * 4);

    for (size_t index = 0; index < length; index += 3)
    {
        const unsigned int octet_a = data[index];
        const unsigned int octet_b = index + 1 < length ? data[index + 1] : 0;
        const unsigned int octet_c = index + 2 < length ? data[index + 2] : 0;

        const unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output.push_back(kBase64[(triple >> 18) & 0x3F]);
        output.push_back(kBase64[(triple >> 12) & 0x3F]);
        output.push_back(index + 1 < length ? kBase64[(triple >> 6) & 0x3F] : '=');
        output.push_back(index + 2 < length ? kBase64[triple & 0x3F] : '=');
    }

    while (!output.empty() && output.back() == '=')
        output.pop_back();

    std::replace(output.begin(), output.end(), '+', '-');
    std::replace(output.begin(), output.end(), '/', '_');
    return output;
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return "";

    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

void WriteTextFile(const std::string& path, const std::string& content)
{
    EnsureAppHome();

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
        throw std::runtime_error("Unable to write " + path);

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void WriteTextFileAtomically(const std::string& path, const std::string& content)
{
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    WriteTextFile(temporary, content);

    std::remove(backup.c_str());
    const bool had_original = std::rename(path.c_str(), backup.c_str()) == 0;
    if (std::rename(temporary.c_str(), path.c_str()) == 0)
        return;

    if (had_original)
        std::rename(backup.c_str(), path.c_str());
    std::remove(temporary.c_str());
    throw std::runtime_error("Unable to replace " + path);
}

std::vector<StreamRegion> FetchServerZoneCatalog(const HttpClient& client) {
    static std::mutex mutex;
    static std::vector<StreamRegion> cached;
    static std::chrono::steady_clock::time_point measured {}, retry_after {};
    std::lock_guard<std::mutex> lock(mutex);
    const auto now = std::chrono::steady_clock::now();
    if (now < retry_after) return {};
    if (!cached.empty() && now - measured < std::chrono::seconds(120)) return cached;
    HttpResponse queue, mapping;
    const std::string agent = "SwitchNOW/" + std::string(kAppVersion);
    auto fetch = [&](const char* url, HttpResponse& response) {
        try { response = client.Get(url, agent, {"Accept: application/json"}, 3, 4, 512*1024); }
        catch (...) { response = {}; }
    };
    std::thread first([&] { fetch("https://api.printedwaste.com/gfn/queue/", queue); });
    std::thread second([&] { fetch("https://remote.printedwaste.com/config/GFN_SERVERID_TO_REGION_MAPPING", mapping); });
    first.join(); second.join();
    gfn::ZoneCatalogDiagnostics diagnostics;
    cached = queue.status_code == 200 && mapping.status_code == 200
        ? gfn::ParseServerZones(queue.body, mapping.body, NowMs(), &diagnostics) : std::vector<StreamRegion> {};
    measured = std::chrono::steady_clock::now();
    if (cached.empty()) retry_after = measured + std::chrono::seconds(30);
    const auto fresh = std::count_if(cached.begin(), cached.end(), [](const auto& r) {
        return server_zone::FreshQueue(r, NowMs());
    });
    routing::Event(GetAppHome(), "zone_catalog", {{"zones", std::to_string(cached.size())},
        {"fresh_queue_estimates", std::to_string(fresh)}, {"queue_http", std::to_string(queue.status_code)},
        {"mapping_http", std::to_string(mapping.status_code)},
        {"queue_stale", std::to_string(diagnostics.stale)}, {"queue_future", std::to_string(diagnostics.future)},
        {"queue_invalid", std::to_string(diagnostics.invalid)}, {"queue_newest_age_ms", std::to_string(diagnostics.newest_age_ms)},
        {"queue_rule", "fresh_only_unknown_is_not_zero"}, {"fallback", cached.empty() ? "regional_catalog" : "none"}});
    return cached;
}

void ApplyRouteHistory(std::vector<StreamRegion>& regions, const std::string& provider) {
    const auto rows = server_route::ParseHistory(ReadTextFile(GetAppHome() + "/server_route_history.json"));
    const auto normalized = server_location::NormalizeStreamingBaseUrl(provider);
    for (auto& region : regions) {
        const auto estimate = server_route::Predict(rows, normalized, region.url, region.ping_ms, NowMs());
        region.route_history_penalty_ms = estimate.penalty_ms;
        region.route_history_samples = estimate.samples;
        region.route_history_redirects = estimate.redirects;
        region.route_history_expected_rtt_ms = estimate.expected_rtt_ms;
        region.route_history_at_ms = estimate.newest_ms;
    }
}

struct PersistedRegionSelection
{
    bool valid = false;
    std::string provider_url;
    std::string selected_url;
    std::string selected_name;
    std::int64_t measured_at_ms = 0;
    std::vector<StreamRegion> regions;
};

PersistedRegionSelection LoadPersistentRegionSelection(
    const std::string& expected_provider_url,
    std::int64_t maximum_age_ms, bool zones_allowed = false)
{
    PersistedRegionSelection cache;
    const std::string body = ReadTextFile(GetRegionSelectionCachePath());
    if (body.empty())
        return cache;

    try
    {
        JsonPtr root = LoadJson(body);
        const int schema = GetInteger(root.get(), "schema");
        // R29 recalculates old winners; their score used unverified route history.
        if (schema != 5 && schema != 6 && schema != 7 && schema != 8)
            return cache;
        if (schema < 8 && maximum_age_ms <= kPersistentRegionCacheLifetimeMs) return cache;

        cache.provider_url = server_location::NormalizeStreamingBaseUrl(
            GetString(root.get(), "provider_url"));
        cache.selected_url = server_location::NormalizeStreamingBaseUrl(
            GetString(root.get(), "selected_url"));
        cache.selected_name = GetString(root.get(), "selected_name");
        json_t* measured_at = json_object_get(root.get(), "measured_at_ms");
        if (json_is_integer(measured_at))
        {
            cache.measured_at_ms = static_cast<std::int64_t>(
                json_integer_value(measured_at));
        }

        const std::string normalized_provider =
            server_location::NormalizeStreamingBaseUrl(expected_provider_url);
        if (cache.provider_url.empty() ||
            cache.provider_url != normalized_provider ||
            cache.selected_url.empty() ||
            !server_location::IsProbeFresh(cache.measured_at_ms, NowMs(), maximum_age_ms))
        {
            return {};
        }

        json_t* entries = json_object_get(root.get(), "regions");
        if (json_is_array(entries))
        {
            size_t index = 0;
            json_t* entry = nullptr;
            json_array_foreach(entries, index, entry)
            {
                StreamRegion region;
                region.name = GetString(entry, "name");
                region.url = server_location::NormalizeStreamingBaseUrl(
                    GetString(entry, "url"));
                region.ping_ms = GetInteger(entry, "ping_ms", -1);
                region.jitter_ms = GetInteger(entry, "jitter_ms", -1);
                region.selection_score_ms =
                    GetInteger(entry, "selection_score_ms", -1);
                region.successful_probes =
                    GetInteger(entry, "successful_probes", 0);
                region.attempted_probes =
                    GetInteger(entry, "attempted_probes", 0);
                region.probe_degraded = json_is_true(json_object_get(entry, "probe_degraded"));
                region.observed_stream_rtt_ms =
                    schema >= 2 ? GetInteger(entry, "observed_stream_rtt_ms", -1) : -1;
                if (schema >= 3) {
                    region.observed_stream_p95_ms = GetInteger(entry, "observed_stream_p95_ms", -1);
                    region.observed_transport_pause_count = GetInteger(entry, "observed_transport_pause_count", 0);
                    region.observed_transport_pause_ms = GetInteger(entry, "observed_transport_pause_ms", 0);
                    region.observed_playback_pause_count = GetInteger(entry, "observed_playback_pause_count", 0);
                    region.observed_playback_pause_ms = GetInteger(entry, "observed_playback_pause_ms", 0);
                    region.observed_session_seconds = GetInteger(entry, "observed_session_seconds", 0);
                    json_t* observed_at = json_object_get(entry, "observed_at_ms");
                    if (json_is_integer(observed_at))
                        region.observed_at_ms = json_integer_value(observed_at);
                }
                region.observed_server_host = GetString(entry, "observed_server_host");
                region.observed_media_endpoint = GetString(entry, "observed_media_endpoint");
                region.zone_id = GetString(entry, "zone_id");
                region.zone_queue_estimate = GetInteger(entry, "zone_queue_estimate", -1);
                if (auto* time = json_object_get(entry, "zone_queue_at_ms"); json_is_integer(time))
                    region.zone_queue_at_ms = json_integer_value(time);
                if (auto* time = json_object_get(entry, "zone_catalog_at_ms"); json_is_integer(time))
                    region.zone_catalog_at_ms = json_integer_value(time);
                if (!region.name.empty() && !region.url.empty())
                    cache.regions.push_back(std::move(region));
            }
        }

        FilterZoneCatalog(cache.regions, zones_allowed);
        ApplyRouteHistory(cache.regions, cache.provider_url);
        cache.selected_url = server_zone::Select(cache.regions, LoadStreamSettings().region, NowMs());
        for (const auto& region : cache.regions)
            if (server_location::NormalizeStreamingBaseUrl(region.url) == cache.selected_url)
                cache.selected_name = region.name;
        cache.valid = !cache.selected_url.empty();
    }
    catch (...)
    {
        return {};
    }
    return cache;
}

void SavePersistentRegionSelection(
    const std::string& provider_url,
    const std::string& selected_url,
    const std::string& selected_name,
    const std::vector<StreamRegion>& regions,
    std::int64_t measured_at_ms = 0)
{
    JsonPtr root(json_object(), &json_decref);
    JsonPtr entries(json_array(), &json_decref);
    json_object_set_new(root.get(), "schema", json_integer(8));
    json_object_set_new(
        root.get(), "provider_url", json_string(provider_url.c_str()));
    json_object_set_new(
        root.get(), "selected_url", json_string(selected_url.c_str()));
    json_object_set_new(
        root.get(), "selected_name", json_string(selected_name.c_str()));
    json_object_set_new(root.get(), "measured_at_ms",
        json_integer(measured_at_ms > 0 ? measured_at_ms : NowMs()));

    for (const StreamRegion& region : regions)
    {
        JsonPtr entry(json_object(), &json_decref);
        json_object_set_new(entry.get(), "name", json_string(region.name.c_str()));
        json_object_set_new(entry.get(), "url", json_string(region.url.c_str()));
        json_object_set_new(entry.get(), "ping_ms", json_integer(region.ping_ms));
        json_object_set_new(entry.get(), "jitter_ms", json_integer(region.jitter_ms));
        json_object_set_new(
            entry.get(), "selection_score_ms",
            json_integer(region.selection_score_ms));
        json_object_set_new(
            entry.get(), "successful_probes",
            json_integer(region.successful_probes));
        json_object_set_new(
            entry.get(), "attempted_probes",
            json_integer(region.attempted_probes));
        json_object_set_new(entry.get(), "probe_degraded", json_boolean(region.probe_degraded));
        json_object_set_new(entry.get(), "observed_stream_rtt_ms",
                            json_integer(region.observed_stream_rtt_ms));
        json_object_set_new(entry.get(), "observed_stream_p95_ms",
                            json_integer(region.observed_stream_p95_ms));
        json_object_set_new(entry.get(), "observed_transport_pause_count",
                            json_integer(region.observed_transport_pause_count));
        json_object_set_new(entry.get(), "observed_transport_pause_ms",
                            json_integer(region.observed_transport_pause_ms));
        json_object_set_new(entry.get(), "observed_playback_pause_count",
                            json_integer(region.observed_playback_pause_count));
        json_object_set_new(entry.get(), "observed_playback_pause_ms",
                            json_integer(region.observed_playback_pause_ms));
        json_object_set_new(entry.get(), "observed_session_seconds",
                            json_integer(region.observed_session_seconds));
        json_object_set_new(entry.get(), "observed_at_ms", json_integer(region.observed_at_ms));
        json_object_set_new(entry.get(), "observed_server_host", json_string(region.observed_server_host.c_str()));
        json_object_set_new(entry.get(), "observed_media_endpoint", json_string(region.observed_media_endpoint.c_str()));
        json_object_set_new(entry.get(), "zone_id", json_string(region.zone_id.c_str()));
        json_object_set_new(entry.get(), "zone_queue_estimate", json_integer(region.zone_queue_estimate));
        json_object_set_new(entry.get(), "zone_queue_at_ms", json_integer(region.zone_queue_at_ms));
        json_object_set_new(entry.get(), "zone_catalog_at_ms", json_integer(region.zone_catalog_at_ms));
        json_array_append_new(entries.get(), json_incref(entry.get()));
    }
    json_object_set_new(root.get(), "regions", json_incref(entries.get()));

    char* dump = json_dumps(root.get(), JSON_INDENT(2));
    if (!dump)
        throw std::runtime_error("Failed to serialize server region cache");
    std::unique_ptr<char, decltype(&std::free)> output(dump, &std::free);
    WriteTextFileAtomically(GetRegionSelectionCachePath(), output.get());
}

std::string FormatResult(Result rc)
{
    char buffer[32] {};
    std::snprintf(buffer, sizeof(buffer), "0x%08x", static_cast<unsigned int>(rc));
    return std::string(buffer);
}

std::string AuthSessionHealth(const AuthSession& session)
{
    const std::int64_t remaining_ms = session.tokens.expires_at_ms - NowMs();
    const std::int64_t remaining_minutes = remaining_ms > 0 ? remaining_ms / 60000 : 0;
    return "user=" + std::string(session.user.user_id.empty() ? "missing" : "present") +
           " accessMin=" + std::to_string(remaining_minutes) +
           " refresh=" + std::string(session.tokens.refresh_token.empty() ? "missing" : "present") +
           " client=" + std::string(session.tokens.client_token.empty() ? "missing" : "present") +
           " persistent=" + std::to_string(session.persistence_enabled ? 1 : 0);
}

void AppendAuthLog(const std::string& line)
{
    if (!StreamDiagnosticsEnabled())
        return;
    EnsureAppHome();

    std::ofstream stream(GetAppHome() + "/auth.log", std::ios::app);
    if (!stream.is_open())
        return;

    stream << '[' << NowMs() << "] " << line << '\n';
}

std::string GetSessionTracePath()
{
    return GetAppHome() + "/session_trace.log";
}

std::string SessionTraceHint()
{
    return StreamDiagnosticsEnabled()
        ? "\nDetails: sdmc:/switch/SwitchNOW/session_trace.log"
        : "\nEnable Settings > Stream > Debug diagnostics for a detailed trace.";
}

void ResetSessionTraceLog(const std::string& reason)
{
    if (!StreamDiagnosticsEnabled())
        return;
    EnsureAppHome();

    std::ofstream stream(GetSessionTracePath(), std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
        return;

    stream << "SwitchNOW session trace\n";
    stream << "reason=" << reason << "\n";
    stream << "timestamp_ms=" << NowMs() << "\n\n";
}

void AppendSessionTraceLog(const std::string& line)
{
    if (!StreamDiagnosticsEnabled())
        return;
    EnsureAppHome();

    std::ofstream stream(GetSessionTracePath(), std::ios::binary | std::ios::app);
    if (!stream.is_open())
        return;

    stream << "[" << NowMs() << "] " << line << '\n';
}

std::string Lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsSensitiveJsonKey(const std::string& key)
{
    const std::string lower = Lowercase(key);
    return lower.find("token") != std::string::npos ||
           lower.find("authorization") != std::string::npos ||
           lower.find("credential") != std::string::npos ||
           lower.find("password") != std::string::npos ||
           lower.find("secret") != std::string::npos ||
           lower.find("jwt") != std::string::npos ||
           lower == "adid" || lower == "adurl" || lower == "mediafileurl";
}

void RedactJsonInPlace(json_t* value)
{
    if (!value)
        return;

    if (json_is_object(value))
    {
        void* iter = json_object_iter(value);
        while (iter)
        {
            const char* key = json_object_iter_key(iter);
            json_t* child = json_object_iter_value(iter);
            void* next = json_object_iter_next(value, iter);

            if (key && IsSensitiveJsonKey(key))
                json_object_set_new(value, key, json_string("<redacted>"));
            else
                RedactJsonInPlace(child);

            iter = next;
        }
        return;
    }

    if (json_is_array(value))
    {
        size_t index;
        json_t* child;
        json_array_foreach(value, index, child)
            RedactJsonInPlace(child);
    }
}

std::string JsonForTrace(const std::string& body)
{
    if (body.empty())
        return "<empty>";

    json_error_t error {};
    JsonPtr root(json_loads(body.c_str(), 0, &error), &json_decref);
    if (!root)
        return body;

    RedactJsonInPlace(root.get());
    char* dump = json_dumps(root.get(), JSON_INDENT(2));
    if (!dump)
        return body;

    std::unique_ptr<char, decltype(&std::free)> output(dump, &std::free);
    return output.get();
}

std::string HeadersForTrace(const std::vector<std::string>& headers)
{
    std::ostringstream out;
    for (const std::string& header : headers)
    {
        const std::string lower = Lowercase(header);
        if (lower.rfind("authorization:", 0) == 0)
            out << "Authorization: <redacted>\n";
        else
            out << header << "\n";
    }
    return out.str();
}

std::string IntFieldText(json_t* object, const char* key)
{
    if (!object || !json_is_object(object))
        return "";

    json_t* value = json_object_get(object, key);
    if (json_is_integer(value))
        return std::to_string(json_integer_value(value));

    if (json_is_string(value))
        return JsonString(value);

    return "";
}

std::string CloudMatchErrorDetails(int http_status, json_t* root)
{
    std::ostringstream out;
    out << "HTTP " << http_status;

    json_t* req_status = root ? json_object_get(root, "requestStatus") : nullptr;
    if (req_status)
    {
        const std::string status_code = IntFieldText(req_status, "statusCode");
        const std::string status_description = GetString(req_status, "statusDescription");
        const std::string unified_error = IntFieldText(req_status, "unifiedErrorCode");

        if (!status_code.empty())
            out << ", statusCode=" << status_code;
        if (!status_description.empty())
            out << ", statusDescription=" << status_description;
        if (!unified_error.empty())
            out << ", unifiedErrorCode=" << unified_error;
    }

    json_t* sess = root ? json_object_get(root, "session") : nullptr;
    if (sess)
    {
        const std::string session_id = GetString(sess, "sessionId");
        const std::string session_status = IntFieldText(sess, "status");
        const std::string error_code = IntFieldText(sess, "errorCode");
        const std::string error_description = GetString(sess, "errorDescription");

        if (!session_id.empty())
            out << ", sessionId=" << session_id;
        if (!session_status.empty())
            out << ", sessionStatus=" << session_status;
        if (!error_code.empty())
            out << ", sessionErrorCode=" << error_code;
        if (!error_description.empty())
            out << ", sessionErrorDescription=" << error_description;
    }

    return out.str();
}

std::string BuildCloudMatchErrorMessage(const std::string& stage, int http_status, json_t* root)
{
    const std::string details = CloudMatchErrorDetails(http_status, root);
    std::string message = stage + " failed: " + details;
    json_t* request_status = root ? json_object_get(root, "requestStatus") : nullptr;
    if (request_status && GetInteger(request_status, "statusCode") == 81) {
        message += "\nThis game is not available with the current GeForce NOW membership. "
                   "If the game page says Premium, upgrade the membership to play it.";
    }
    if (request_status && cloud_session::IsDeviceLimitError(
            GetInteger(request_status, "statusCode"),
            GetString(request_status, "statusDescription"))) {
        message += "\nGeForce NOW still reports another session for this device. "
                   "SwitchNOW tried to close it automatically. Wait about a minute and retry "
                   "if NVIDIA is still releasing the previous cloud rig.";
    }
    if (request_status && ad_break::IsWaitingAdsTimeout(
            GetInteger(request_status, "statusCode"),
            GetString(request_status, "statusDescription"))) {
        message += "\nNVIDIA ended this Free-tier launch because its required advertisement "
                   "did not complete. Retry the launch; SwitchNOW will play the ad and report "
                   "its real playback progress before continuing to the cloud rig.";
    }
    return message + SessionTraceHint();
}

bool IsAppPatchingResponse(json_t* root)
{
    json_t* request_status = root ? json_object_get(root, "requestStatus") : nullptr;
    if (!request_status || GetInteger(request_status, "statusCode") != 41)
        return false;

    return GetString(request_status, "statusDescription").find("APP_PATCHING_STATUS") != std::string::npos;
}

bool IsDeviceSessionLimitResponse(json_t* root)
{
    json_t* request_status = root ? json_object_get(root, "requestStatus") : nullptr;
    return request_status && cloud_session::IsDeviceLimitError(
        GetInteger(request_status, "statusCode"),
        GetString(request_status, "statusDescription"));
}

std::string GetSessionServerIp(json_t* session)
{
    // List responses may contain placeholder hosts or incomplete IP arrays.
    // Use the same validated selection as ordinary session polling.
    const auto host=gfn::endpoints::Resolve(session).control_host;
    if(host.empty()) routing::Event(GetAppHome(),"cleanup_hint",{{"result","unusable_or_missing"}});
    return host;
}

void ApplySessionAds(json_t* sess, SessionInfo& info)
{
    if (!sess || !json_is_object(sess))
        return;

    info.ad_break.required = GetBool(sess, "sessionAdsRequired");
    info.ad_break.provider = GetInteger(sess, "adProvider");

    json_t* ads = json_object_get(sess, "sessionAds");
    if (!ads || !json_is_array(ads))
        return;

    // ReportSessionAd responses replace the current break. Never append a
    // refreshed server list to metadata retained by the caller.
    info.ad_break.ads.clear();

    if (json_array_size(ads) > 0)
        info.ad_break.required = true;

    size_t ad_index = 0;
    json_t* ad_object = nullptr;
    json_array_foreach(ads, ad_index, ad_object)
    {
        if (!ad_object || !json_is_object(ad_object))
            continue;

        SessionAdInfo ad;
        ad.id = GetString(ad_object, "adId");
        ad.state = GetInteger(ad_object, "adState");
        ad.duration_seconds = GetInteger(ad_object, "adLengthInSeconds");

        json_t* media_files = json_object_get(ad_object, "adMediaFiles");
        if (media_files && json_is_array(media_files))
        {
            size_t media_index = 0;
            json_t* media_object = nullptr;
            json_array_foreach(media_files, media_index, media_object)
            {
                if (!media_object || !json_is_object(media_object))
                    continue;

                AdMediaFileInfo media;
                media.url = GetString(media_object, "mediaFileUrl");
                if (media.url.empty())
                    media.url = GetString(media_object, "url");
                if (media.url.empty())
                    media.url = GetString(media_object, "adUrl");
                if (media.url.empty())
                    media.url = GetString(media_object, "mediaUrl");
                media.encoding_profile = GetString(media_object, "encodingProfile");
                if (media.encoding_profile.empty())
                    media.encoding_profile = GetString(media_object, "profile");
                if (!media.url.empty())
                    ad.media_files.push_back(std::move(media));
            }
        }

        if (ad.media_files.empty())
        {
            const std::string fallback_url = GetString(ad_object, "adUrl");
            if (!fallback_url.empty())
                ad.media_files.push_back({fallback_url, "default"});
        }

        info.ad_break.ads.push_back(std::move(ad));
    }
}

std::string GenerateDeviceId()
{
    std::string stored = Trim(ReadTextFile(GetDeviceIdPath()));
    if (!stored.empty())
        return stored;

    const auto bytes = GenerateRandomBytes(32);
    stored           = HexEncode(bytes.data(), bytes.size());
    WriteTextFileAtomically(GetDeviceIdPath(), stored);
    return stored;
}

constexpr const char* kTokenVaultHeader = "OPENNOW_TOKEN_VAULT_V1";
constexpr const char* kTokenVaultAad = "OpenNOW Switch token vault v1";

std::array<unsigned char, 32> TokenVaultKey()
{
    const std::string material =
        "OpenNOW-Switch-vault-key-v1|" + GenerateDeviceId() + "|05004F4E4F575358";
    std::array<unsigned char, 32> key {};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(material.data()),
                       material.size(), key.data(), 0) != 0)
    {
        throw std::runtime_error("Unable to derive token vault key");
    }
    return key;
}

std::string EncryptTokenVault(const std::string& plaintext)
{
    const auto key = TokenVaultKey();
    const auto nonce = GenerateRandomBytes(12);
    std::array<unsigned char, 16> tag {};
    std::vector<unsigned char> ciphertext(plaintext.size());

    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int rc = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (rc == 0)
    {
        rc = mbedtls_gcm_crypt_and_tag(
            &context, MBEDTLS_GCM_ENCRYPT, plaintext.size(), nonce.data(), nonce.size(),
            reinterpret_cast<const unsigned char*>(kTokenVaultAad), std::strlen(kTokenVaultAad),
            reinterpret_cast<const unsigned char*>(plaintext.data()), ciphertext.data(),
            tag.size(), tag.data());
    }
    mbedtls_gcm_free(&context);
    if (rc != 0)
        throw std::runtime_error("Unable to encrypt token vault");

    return std::string(kTokenVaultHeader) + "\n" +
           HexEncode(nonce.data(), nonce.size()) + "\n" +
           HexEncode(tag.data(), tag.size()) + "\n" +
           HexEncode(ciphertext.data(), ciphertext.size()) + "\n";
}

std::string DecryptTokenVault(const std::string& encoded)
{
    std::istringstream input(encoded);
    std::string header;
    std::string nonce_hex;
    std::string tag_hex;
    std::string ciphertext_hex;
    std::getline(input, header);
    std::getline(input, nonce_hex);
    std::getline(input, tag_hex);
    std::getline(input, ciphertext_hex);
    if (header != kTokenVaultHeader)
        return encoded;

    const auto nonce = HexDecode(nonce_hex);
    const auto tag = HexDecode(tag_hex);
    const auto ciphertext = HexDecode(ciphertext_hex);
    if (nonce.size() != 12 || tag.size() != 16 || ciphertext.empty())
        throw std::runtime_error("Invalid token vault structure");

    const auto key = TokenVaultKey();
    std::string plaintext(ciphertext.size(), '\0');
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int rc = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (rc == 0)
    {
        rc = mbedtls_gcm_auth_decrypt(
            &context, ciphertext.size(), nonce.data(), nonce.size(),
            reinterpret_cast<const unsigned char*>(kTokenVaultAad), std::strlen(kTokenVaultAad),
            tag.data(), tag.size(), ciphertext.data(),
            reinterpret_cast<unsigned char*>(plaintext.data()));
    }
    mbedtls_gcm_free(&context);
    if (rc != 0)
        throw std::runtime_error("Token vault authentication failed");
    return plaintext;
}

struct PkcePair
{
    std::string verifier;
    std::string challenge;
};

PkcePair GeneratePkce()
{
    const auto verifier_bytes = GenerateRandomBytes(64);
    PkcePair result;
    result.verifier = Base64UrlEncode(verifier_bytes.data(), verifier_bytes.size());
    if (result.verifier.size() > 86)
        result.verifier.resize(86);

    std::array<unsigned char, 32> hash {};
#ifdef __SWITCH__
    sha256CalculateHash(hash.data(), result.verifier.data(), result.verifier.size());
#else
    throw std::runtime_error("PKCE generation is only supported in the Switch build");
#endif
    result.challenge = Base64UrlEncode(hash.data(), hash.size());
    return result;
}

std::string UrlEncode(const std::string& input, bool plus_for_space = false)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.size() * 3);

    for (unsigned char ch : input)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            output.push_back(static_cast<char>(ch));
        }
        else if (plus_for_space && ch == ' ')
        {
            output.push_back('+');
        }
        else
        {
            output.push_back('%');
            output.push_back(kDigits[(ch >> 4) & 0x0F]);
            output.push_back(kDigits[ch & 0x0F]);
        }
    }

    return output;
}

std::string UrlDecode(const std::string& input)
{
    std::string output;
    output.reserve(input.size());

    for (size_t index = 0; index < input.size(); ++index)
    {
        if (input[index] == '+' )
        {
            output.push_back(' ');
            continue;
        }

        if (input[index] == '%' && index + 2 < input.size())
        {
            const auto hex = input.substr(index + 1, 2);
            char* end      = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end && *end == '\0')
            {
                output.push_back(static_cast<char>(value));
                index += 2;
                continue;
            }
        }

        output.push_back(input[index]);
    }

    return output;
}

std::string GetQueryValue(const std::string& url, const std::string& key)
{
    const auto query_pos = url.find('?');
    if (query_pos == std::string::npos)
        return "";

    size_t current = query_pos + 1;
    while (current < url.size())
    {
        const auto next      = url.find('&', current);
        const std::string kv = url.substr(current, next == std::string::npos ? std::string::npos : next - current);
        const auto equals    = kv.find('=');
        const std::string raw_key =
            equals == std::string::npos ? kv : kv.substr(0, equals);

        if (UrlDecode(raw_key) == key)
        {
            const std::string raw_value =
                equals == std::string::npos ? std::string() : kv.substr(equals + 1);
            return UrlDecode(raw_value);
        }

        if (next == std::string::npos)
            break;

        current = next + 1;
    }

    return "";
}

std::vector<std::string> BuildNvidiaAuthHeaders(
    const std::string& bearer_token = {},
    const std::string& content_type = {},
    bool include_referer            = false,
    const std::string& accept       = "application/json, text/plain, */*")
{
    std::vector<std::string> headers;
    headers.push_back("Origin: " + std::string(kNvidiaFileOrigin));
    headers.push_back("Accept: " + accept);
    headers.push_back("User-Agent: " + std::string(GfnClient::kUserAgent));

    if (!bearer_token.empty())
        headers.push_back("Authorization: Bearer " + bearer_token);

    if (!content_type.empty())
        headers.push_back("Content-Type: " + content_type);

    if (include_referer)
        headers.push_back("Referer: " + std::string(kNvidiaFileReferer));

    return headers;
}

std::vector<std::string> BuildGfnLcarsHeaders(
    const std::string& token,
    const std::string& client_type,
    const std::string& client_streamer,
    bool include_user_agent)
{
    std::vector<std::string> headers;
    headers.push_back("Accept: application/json");
    headers.push_back("nv-client-id: " + std::string(kLcarsClientId));
    headers.push_back("nv-client-type: " + client_type);
    headers.push_back("nv-client-version: " + std::string(kClientVersion));
    headers.push_back("nv-client-streamer: " + client_streamer);
    headers.push_back("nv-device-os: WINDOWS");
    headers.push_back("nv-device-type: DESKTOP");

    if (!token.empty())
        headers.push_back("Authorization: GFNJWT " + token);

    if (include_user_agent)
        headers.push_back("User-Agent: " + std::string(GfnClient::kUserAgent));

    return headers;
}

std::vector<std::string> BuildMembershipHeaders(const std::string& token)
{
    return {
        "Accept: application/json",
        "Authorization: GFNJWT " + token,
        "nv-client-id: " + std::string(kLcarsClientId),
        "nv-client-type: NATIVE",
        "nv-client-version: " + std::string(kClientVersion),
        "nv-client-streamer: NVIDIA-CLASSIC",
        "nv-device-os: WINDOWS",
        "nv-device-type: DESKTOP",
        "nv-device-make: UNKNOWN",
        "nv-device-model: UNKNOWN",
    };
}

std::vector<int> MeasureRegionLatencySamples(
    const HttpClient& http_client,
    const std::string& url,
    int probe_count,
    long timeout_ms, const char* phase, std::chrono::steady_clock::time_point deadline,
    int& attempted)
{
    std::vector<int> samples;
    std::ostringstream outcomes;
    std::ostringstream codes, dns, tls, os_errors, ips;
    std::string detail;
    samples.reserve(static_cast<size_t>(std::max(0, probe_count)));
    const int previous_attempts = attempted;
    for (int attempt = 0; attempt < probe_count; ++attempt)
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) break;
        if (!network::ProbeAllowed()) break;
        TcpProbeDiagnostics diagnostics;
        const int ping_ms =
            http_client.MeasureTcpHandshakeLatencyMs(url, std::min<long>(timeout_ms, remaining), &diagnostics);
        if (diagnostics.suppressed) break;
        ++attempted;
        if (ping_ms >= 0)
            samples.push_back(ping_ms);
        if (attempt) { outcomes << ';'; codes << ';'; dns << ';'; tls << ';'; os_errors << ';'; ips << ';'; }
        outcomes << ping_ms;
        codes << diagnostics.curl_code; dns << diagnostics.dns_ms; tls << diagnostics.tls_ms;
        os_errors << diagnostics.os_error; ips << diagnostics.remote_ip;
        if (!diagnostics.detail.empty()) detail = diagnostics.detail;
        if (diagnostics.resource_exhausted) break;
        if (attempt + 1 < probe_count)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    routing::Event(GetAppHome(), "probe", {{"phase", phase}, {"host", routing::Host(url)},
        {"timeout_ms", std::to_string(timeout_ms)}, {"outcomes_ms", outcomes.str()},
        {"curl_codes", codes.str()}, {"os_errors", os_errors.str()}, {"remote_ips", ips.str()},
        {"detail", detail}, {"kind", "TCP_only_no_TLS"}, {"dns_ms", dns.str()}, {"tls_ms", tls.str()},
        {"successful", std::to_string(samples.size())}, {"attempted", std::to_string(attempted - previous_attempts)}});
    return samples;
}

std::vector<std::string> BuildGraphQlHeaders(const std::string& token)
{
    std::vector<std::string> headers;
    headers.push_back("Accept: application/json, text/plain, */*");
    // NVIDIA's Switch-facing persisted-query endpoint rejects this GET as
    // malformed JSON. application/graphql keeps both library and play-time
    // fields available while preserving the endpoint's expected request type.
    headers.push_back("Content-Type: application/graphql");
    headers.push_back("Origin: " + std::string(kPlayOrigin));
    headers.push_back("Referer: " + std::string(kPlayReferer));
    headers.push_back("nv-client-id: " + std::string(kLcarsClientId));
    headers.push_back("nv-client-type: NATIVE");
    headers.push_back("nv-client-version: " + std::string(kClientVersion));
    headers.push_back("nv-client-streamer: NVIDIA-CLASSIC");
    headers.push_back("nv-device-os: WINDOWS");
    headers.push_back("nv-device-type: DESKTOP");
    headers.push_back("nv-device-make: UNKNOWN");
    headers.push_back("nv-device-model: UNKNOWN");
    headers.push_back("nv-browser-type: CHROME");
    headers.push_back("User-Agent: " + std::string(GfnClient::kUserAgent));

    if (!token.empty())
        headers.push_back("Authorization: GFNJWT " + token);

    return headers;
}

std::int64_t ToExpiresAtMs(json_t* payload, const char* key, std::int64_t fallback_ms = 24LL * 60LL * 60LL * 1000LL)
{
    if (payload && json_is_object(payload))
    {
        json_t* expires = json_object_get(payload, key);
        if (json_is_integer(expires))
            return NowMs() + static_cast<std::int64_t>(json_integer_value(expires)) * 1000LL;
    }

    return NowMs() + fallback_ms;
}

bool IsNearExpiry(std::int64_t expires_at_ms)
{
    return auth::ShouldRefresh(expires_at_ms, NowMs(), kRefreshWindowMs);
}

bool IsExpired(std::int64_t expires_at_ms)
{
    return auth::IsExpired(expires_at_ms, NowMs());
}

struct NativeAuthResponse
{
    long status_code = 0;
    std::string body;
    std::string location;
};

class NativeAuthHttpSession
{
  public:
    NativeAuthHttpSession()
        : curl_(curl_easy_init(), &curl_easy_cleanup)
    {
        if (!curl_)
            throw std::runtime_error("Unable to initialize the native NVIDIA login connection");
    }

    NativeAuthResponse Request(
        const std::string& method,
        const std::string& url,
        const std::vector<std::string>& headers = {},
        const std::string& body = {})
    {
        auto cancellation = HttpCancellationScope::Callback();
        if (cancellation && cancellation()) throw std::runtime_error("Session recovery cancelled");
        const std::string safe_url = SanitizeRuntimeUrl(url);
        const auto operation_id = BeginRuntimeOperation(
            "auth_transport", method, "url=" + safe_url + " connection=reused");
        const auto started_at = std::chrono::steady_clock::now();
        curl_easy_reset(curl_.get());
        response_body_.clear();
        response_location_.clear();
        error_buffer_.fill('\0');

        curl_slist* raw_headers = nullptr;
        for (const auto& header : headers)
            raw_headers = curl_slist_append(raw_headers, header.c_str());
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(
            raw_headers, &curl_slist_free_all);

        curl_easy_setopt(curl_.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_.get(), CURLOPT_USERAGENT, GfnClient::kUserAgent);
        curl_easy_setopt(curl_.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl_.get(), CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_.get(), CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl_.get(), CURLOPT_NOSIGNAL, 1L);
        if (cancellation) {
            curl_easy_setopt(curl_.get(), CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl_.get(), CURLOPT_XFERINFODATA, &cancellation);
            curl_easy_setopt(curl_.get(), CURLOPT_XFERINFOFUNCTION,
                +[](void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                    return (*static_cast<std::function<bool()>*>(context))() ? 1 : 0;
                });
        }
        curl_easy_setopt(curl_.get(), CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl_.get(), CURLOPT_ERRORBUFFER, error_buffer_.data());
        curl_easy_setopt(curl_.get(), CURLOPT_COOKIEFILE, "");
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, &NativeAuthHttpSession::WriteBody);
        curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl_.get(), CURLOPT_HEADERFUNCTION, &NativeAuthHttpSession::WriteHeader);
        curl_easy_setopt(curl_.get(), CURLOPT_HEADERDATA, this);
        if (header_list)
            curl_easy_setopt(curl_.get(), CURLOPT_HTTPHEADER, header_list.get());

        if (method == "POST")
        {
            curl_easy_setopt(curl_.get(), CURLOPT_POST, 1L);
            curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl_.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
        else
        {
            curl_easy_setopt(curl_.get(), CURLOPT_HTTPGET, 1L);
        }

        const CURLcode rc = curl_easy_perform(curl_.get());
        if (rc != CURLE_OK)
        {
            char* primary_ip = nullptr;
            curl_easy_getinfo(curl_.get(), CURLINFO_PRIMARY_IP, &primary_ip);
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at).count();
            const std::string detail =
                "url=" + safe_url +
                " curl_code=" + std::to_string(static_cast<int>(rc)) +
                " curl_error=" +
                    (error_buffer_[0] ? std::string(error_buffer_.data())
                                      : std::string(curl_easy_strerror(rc))) +
                " elapsed_ms=" + std::to_string(elapsed_ms) +
                " primary_ip=" + (primary_ip ? std::string(primary_ip) : std::string("unknown"));
            EndRuntimeOperation(operation_id, "auth_transport", method, "error", detail);
            AppendAuthLog("auth-transport: " + detail);
            throw std::runtime_error(
                std::string("Native NVIDIA login network error: ") +
                (error_buffer_[0] ? error_buffer_.data() : curl_easy_strerror(rc)));
        }

        long status_code = 0;
        curl_easy_getinfo(curl_.get(), CURLINFO_RESPONSE_CODE, &status_code);
        char* primary_ip = nullptr;
        curl_easy_getinfo(curl_.get(), CURLINFO_PRIMARY_IP, &primary_ip);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
        EndRuntimeOperation(
            operation_id, "auth_transport", method, "ok",
            "url=" + safe_url +
                " status=" + std::to_string(status_code) +
                " bytes=" + std::to_string(response_body_.size()) +
                " elapsed_ms=" + std::to_string(elapsed_ms) +
                " primary_ip=" + (primary_ip ? std::string(primary_ip) : std::string("unknown")));
        return {status_code, response_body_, response_location_};
    }

  private:
    static size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* self = static_cast<NativeAuthHttpSession*>(userdata);
        const size_t bytes = size * nmemb;
        self->response_body_.append(ptr, bytes);
        return bytes;
    }

    static size_t WriteHeader(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* self = static_cast<NativeAuthHttpSession*>(userdata);
        const size_t bytes = size * nmemb;
        std::string line(ptr, bytes);
        if (line.size() >= 9)
        {
            std::string prefix = line.substr(0, 9);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (prefix == "location:")
                self->response_location_ = Trim(line.substr(9));
        }
        return bytes;
    }

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_;
    std::array<char, CURL_ERROR_SIZE> error_buffer_ {};
    std::string response_body_;
    std::string response_location_;
};

std::string ResolveRedirectUrl(const std::string& current_url, const std::string& location)
{
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0)
        return location;
    if (location.empty() || location.front() != '/')
        throw std::runtime_error("NVIDIA login returned an unsupported redirect");

    const auto scheme_end = current_url.find("://");
    const auto host_end = scheme_end == std::string::npos
        ? std::string::npos
        : current_url.find('/', scheme_end + 3);
    if (scheme_end == std::string::npos)
        throw std::runtime_error("NVIDIA login returned an invalid redirect origin");
    return current_url.substr(0, host_end) + location;
}

std::vector<std::string> NativeAuthHeaders(const std::string& key, bool metrics = false)
{
    std::vector<std::string> headers {
        "Accept: application/json",
        "Content-Type: application/json",
        "Accept-Language: en-US",
        "Authorization: Bearer " + key,
        "Origin: https://login.nvgs.nvidia.com",
        "Referer: https://login.nvgs.nvidia.com/",
    };
    if (metrics)
    {
        headers.push_back(
            "x-metrics: 5ZjiARedWmKOoXlrQRFkblcGoDlJGPOT0oHz4C03hOS72FpYLX0LRmdNk8GbhBIJ");
    }
    return headers;
}

std::string DumpJson(json_t* value)
{
    char* raw = json_dumps(value, JSON_COMPACT);
    if (!raw)
        throw std::runtime_error("Unable to encode the NVIDIA login request");
    std::string result(raw);
    free(raw);
    return result;
}

std::string NativeErrorCode(const std::string& body)
{
    try
    {
        JsonPtr root = LoadJson(body);
        std::string error = GetString(root.get(), "error");
        if (error.empty())
            error = GetString(root.get(), "error_description");
        return error;
    }
    catch (...)
    {
        return {};
    }
}

JsonPtr NativeJsonRequest(
    NativeAuthHttpSession& session,
    const std::string& method,
    const std::string& url,
    const std::string& key,
    json_t* payload,
    bool metrics = false)
{
    const NativeAuthResponse response = session.Request(
        method, url, NativeAuthHeaders(key, metrics), payload ? DumpJson(payload) : std::string {});
    if (response.status_code < 200 || response.status_code >= 300)
    {
        const std::string code = NativeErrorCode(response.body);
        AppendAuthLog(
            "auth-native: request failed HTTP=" + std::to_string(response.status_code) +
            " error=" + (code.empty() ? "unknown" : code));
        if (code == "VALIDATION_REQUIRED" || code == "CAPTCHA_REQUIRED")
            throw NativeLoginFallbackRequired("NVIDIA requires a CAPTCHA for this sign-in");
        if (code == "CREDENTIALS_INVALID" || code == "UNAUTHORIZED")
            throw std::runtime_error("Incorrect NVIDIA email, password, or verification code");
        if (code == "ITEM_NOT_FOUND")
            throw std::runtime_error("No NVIDIA account was found for this email");
        throw std::runtime_error(
            "NVIDIA native login failed (HTTP " + std::to_string(response.status_code) +
            (code.empty() ? ")" : ", " + code + ")"));
    }
    return LoadJson(response.body);
}

struct NativeStage
{
    std::string page;
    std::string key;
    std::string external_url;
    JsonPtr payload {nullptr, &json_decref};
};

NativeStage AdvanceNativeStage(NativeAuthHttpSession& session, const std::string& key)
{
    JsonPtr empty(json_object(), &json_decref);
    JsonPtr root = NativeJsonRequest(
        session, "POST", "https://accounts.nvgs.nvidia.com/api/1/frontend/oauth/user/next",
        key, empty.get());
    NativeStage stage;
    stage.page = GetString(root.get(), "page");
    stage.key = GetString(root.get(), "key");
    stage.external_url = GetString(root.get(), "externalUrl");
    stage.payload = std::move(root);
    if (stage.key.empty())
        stage.key = key;
    AppendAuthLog("auth-native: stage=" + (stage.page.empty() ? "unknown" : stage.page));
    return stage;
}

std::string BuildAuthorizeUrl(const LoginProvider& provider, const PkcePair& pkce,
                              const std::string& state,
                              const std::string& redirect_uri = std::string(kRedirectUri),
                              const std::string& login_hint = {})
{
    const auto nonce = HexEncode(GenerateRandomBytes(16).data(), 16);

    std::string url = std::string(kAuthorizeEndpoint) + "?";
    url += "response_type=code";
    url += "&device_id=" + UrlEncode(GenerateDeviceId());
    url += "&scope=" + UrlEncode(kScopes, true);
    url += "&client_id=" + UrlEncode(kClientId);
    url += "&redirect_uri=" + UrlEncode(redirect_uri);
    url += "&ui_locales=" + UrlEncode("en_US");
    url += "&nonce=" + UrlEncode(nonce);
    url += "&state=" + UrlEncode(state);
    url += "&prompt=" + UrlEncode(login_hint.empty() ? "select_account" : "login");
    if (!login_hint.empty())
        url += "&login_hint=" + UrlEncode(login_hint);
    url += "&code_challenge=" + UrlEncode(pkce.challenge);
    url += "&code_challenge_method=S256";
    url += "&idp_id=" + UrlEncode(provider.idp_id);
    return url;
}

#ifdef __SWITCH__
class OAuthCallbackServer
{
  public:
    explicit OAuthCallbackServer(WebCommonConfig* browser_config)
        : browser_config_(browser_config)
    {
    }

    ~OAuthCallbackServer()
    {
        Stop();
    }

    void Start()
    {
        worker_ = std::thread([this]() {
            Run();
        });
    }

    void Stop()
    {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    std::string WaitForResult()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, std::chrono::seconds(8), [this]() {
                return completed_;
            }))
        {
            return "";
        }

        if (!error_.empty())
            throw std::runtime_error(error_);

        return callback_url_;
    }

  private:
    void Complete(std::string callback_url, std::string error = {})
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completed_)
                return;

            callback_url_ = std::move(callback_url);
            error_        = std::move(error);
            completed_    = true;
        }

        condition_.notify_all();

        if (browser_config_)
            webConfigRequestExit(browser_config_);
    }

    void SendResponse(int client, bool ok)
    {
        const std::string body =
            ok
                ? "<!doctype html><html><body style=\"font-family:sans-serif;background:#101418;color:#eef;padding:32px\"><h2>SwitchNOW login complete</h2><p>You can return to SwitchNOW now.</p></body></html>"
                : "<!doctype html><html><body style=\"font-family:sans-serif;background:#101418;color:#eef;padding:32px\"><h2>SwitchNOW login failed</h2><p>Return to SwitchNOW and check auth.log.</p></body></html>";

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;

        send(client, response.data(), response.size(), 0);
    }

    void Run()
    {
        const int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0)
        {
            Complete({}, "OAuth callback server socket() failed");
            return;
        }

        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(kLoginCallbackPort);
        address.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        {
            close(server);
            Complete({}, std::string("OAuth callback server bind() failed on localhost:") + std::to_string(kLoginCallbackPort));
            return;
        }

        if (listen(server, 1) < 0)
        {
            close(server);
            Complete({}, "OAuth callback server listen() failed");
            return;
        }

        AppendAuthLog(std::string("auth: callback server listening on localhost:") + std::to_string(kLoginCallbackPort));

        while (!stop_)
        {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server, &read_fds);

            timeval timeout {};
            timeout.tv_sec = 1;

            const int ready = select(server + 1, &read_fds, nullptr, nullptr, &timeout);
            if (ready <= 0)
                continue;

            const int client = accept(server, nullptr, nullptr);
            if (client < 0)
                continue;

            char buffer[2048] {};
            const ssize_t read = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (read <= 0)
            {
                close(client);
                continue;
            }

            const std::string request(buffer, static_cast<size_t>(read));
            const auto first_space  = request.find(' ');
            const auto second_space = first_space == std::string::npos
                                          ? std::string::npos
                                          : request.find(' ', first_space + 1);

            std::string target =
                first_space == std::string::npos || second_space == std::string::npos
                    ? std::string("/")
                    : request.substr(first_space + 1, second_space - first_space - 1);

            if (target.rfind("http://", 0) != 0)
                target = std::string(kRedirectUri) + target;

            const bool ok = !GetQueryValue(target, "code").empty() || !GetQueryValue(target, "error").empty();
            SendResponse(client, ok);
            close(client);

            AppendAuthLog(
                "auth: callback server received code=" +
                std::string(GetQueryValue(target, "code").empty() ? "no" : "yes") +
                " error=" +
                std::string(GetQueryValue(target, "error").empty() ? "no" : "yes"));

            if (ok)
            {
                Complete(target);
                break;
            }
        }

        close(server);
    }

    WebCommonConfig* browser_config_ = nullptr;
    std::atomic<bool> stop_ {false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool completed_ = false;
    std::string callback_url_;
    std::string error_;
};

std::string ExtractCallbackUrlFromReply(WebCommonReply* reply)
{
    char last_url[0x1000] {};
    size_t last_url_size = 0;

    Result rc = webReplyGetLastUrl(reply, last_url, sizeof(last_url), &last_url_size);
    if (R_SUCCEEDED(rc) && last_url[0] != '\0')
        return std::string(last_url);

    if (!reply->type && reply->ret.lastUrl[0] != '\0')
        return std::string(reply->ret.lastUrl);

    return "";
}
#endif

std::string RunBrowserLogin(const std::string& auth_url)
{
#ifdef __SWITCH__
    const AppletType applet_type = appletGetAppletType();
    if (applet_type != AppletType_Application && applet_type != AppletType_SystemApplication)
    {
        AppendAuthLog("auth: refused browser login outside application mode");
        throw std::runtime_error(
            "NVIDIA login needs Switch application mode. Launch Homebrew Menu with title override: hold R while opening any installed game, then start SwitchNOW from there.");
    }

    AppendAuthLog("auth: creating browser applet");

    WebCommonConfig config {};
    Result rc = webPageCreate(&config, auth_url.c_str());
    if (R_FAILED(rc))
    {
        AppendAuthLog("auth: webPageCreate failed " + FormatResult(rc));
        throw std::runtime_error("Unable to open the Switch browser applet: " + FormatResult(rc));
    }

    rc = webConfigSetWhitelist(&config, "^https?://.*");
    if (R_FAILED(rc))
    {
        AppendAuthLog("auth: webConfigSetWhitelist failed " + FormatResult(rc));
        throw std::runtime_error("Failed to configure the browser whitelist: " + FormatResult(rc));
    }

    const Result callback_rc = webConfigSetCallbackUrl(&config, kRedirectUri);
    const bool system_callback = R_SUCCEEDED(callback_rc);
    AppendAuthLog(
        "auth: system callback mode=" + std::string(system_callback ? "enabled" : "unavailable") +
        " rc=" + FormatResult(callback_rc));

    std::unique_ptr<OAuthCallbackServer> callback_server;
    if (!system_callback)
    {
        callback_server = std::make_unique<OAuthCallbackServer>(&config);
        callback_server->Start();
    }

    WebCommonReply reply {};
    AppendAuthLog("auth: showing browser applet");
    rc = webConfigShow(&config, &reply);
    if (R_FAILED(rc))
    {
        AppendAuthLog("auth: webConfigShow failed " + FormatResult(rc));
        throw std::runtime_error("The Switch browser closed before login completed: " + FormatResult(rc));
    }

    std::string callback_url = ExtractCallbackUrlFromReply(&reply);
    if (callback_url.empty() && callback_server)
        callback_url = callback_server->WaitForResult();
    if (callback_server)
        callback_server->Stop();

    if (!callback_url.empty())
        return callback_url;

    WebExitReason exit_reason = WebExitReason_UnknownE;
    rc                        = webReplyGetExitReason(&reply, &exit_reason);
    if (R_SUCCEEDED(rc))
        AppendAuthLog("auth: browser exit reason without callback " + std::to_string(static_cast<int>(exit_reason)));

    throw std::runtime_error("GeForce NOW login browser closed before the OAuth callback was received");
#else
    (void)auth_url;
    throw std::runtime_error("Browser login is only supported in the Switch build");
#endif
}

AuthTokens ParseAuthTokens(json_t* payload)
{
    AuthTokens tokens;
    tokens.access_token              = GetString(payload, "access_token");
    tokens.refresh_token             = GetString(payload, "refresh_token");
    tokens.id_token                  = GetString(payload, "id_token");
    tokens.client_token              = GetString(payload, "client_token");
    tokens.expires_at_ms             = ToExpiresAtMs(payload, "expires_in");
    tokens.client_token_expires_at_ms = ToExpiresAtMs(payload, "expires_in");

    if (tokens.access_token.empty())
        throw std::runtime_error("Login succeeded but no access token was returned");

    return tokens;
}

void WarmNvidiaTokenConnection(
    NativeAuthHttpSession& session,
    const std::string& access_token = {})
{
    // libnx curl 7.69 can fail a fresh POST handshake to login.nvidia.com even though
    // the interactive authorize connection is healthy. A verified GET on the same
    // easy handle establishes the TLS session that the token POST can safely reuse.
    const std::string url = access_token.empty() ? kAuthorizeEndpoint : kUserInfoEndpoint;
    const auto headers = BuildNvidiaAuthHeaders(
        access_token, {}, false, access_token.empty() ? "text/html,application/json" : "application/json");
    const NativeAuthResponse response = session.Request("GET", url, headers);
    AppendAuthLog(
        "auth-transport: preflight complete endpoint=" + SanitizeRuntimeUrl(url) +
        " HTTP=" + std::to_string(response.status_code));
}

AuthTokens ExchangeAuthorizationCode(
    NativeAuthHttpSession& session,
    const std::string& code,
    const std::string& verifier,
    const std::string& redirect_uri = std::string(kRedirectUri))
{
    const std::string body =
        "grant_type=authorization_code"
        "&code=" + UrlEncode(code, true) +
        "&redirect_uri=" + UrlEncode(redirect_uri, true) +
        "&code_verifier=" + UrlEncode(verifier, true);

    const NativeAuthResponse response = session.Request(
        "POST",
        kTokenEndpoint,
        BuildNvidiaAuthHeaders({}, "application/x-www-form-urlencoded; charset=UTF-8", true),
        body);
    if (response.status_code != 200)
    {
        AppendAuthLog(
            "auth: token exchange failed HTTP " + std::to_string(response.status_code) +
            " body=" + JsonForTrace(response.body).substr(0, 240));
        throw std::runtime_error(
            "Token exchange failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    AppendAuthLog("auth-transport: authorization code exchanged on persistent connection");
    return ParseAuthTokens(root.get());
}

void RequestClientToken(NativeAuthHttpSession& session, AuthTokens& tokens);

AuthTokens MergeRefreshedTokens(AuthTokens tokens, const AuthSession& auth_session)
{
    if (tokens.refresh_token.empty())
        tokens.refresh_token = auth_session.tokens.refresh_token;
    if (tokens.id_token.empty())
        tokens.id_token = auth_session.tokens.id_token;
    if (tokens.client_token.empty())
    {
        tokens.client_token = auth_session.tokens.client_token;
        tokens.client_token_expires_at_ms = auth_session.tokens.client_token_expires_at_ms;
    }
    else if (tokens.client_token != auth_session.tokens.client_token)
    {
        // The OAuth response does not describe the lifetime of a rotated client token.
        // Force /client_token to return authoritative lifetime metadata below.
        tokens.client_token_expires_at_ms = 0;
    }
    return tokens;
}

NativeAuthResponse PostTokenGrant(
    NativeAuthHttpSession& session,
    const std::string& body,
    const std::string& grant_name)
{
    NativeAuthResponse response;
    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        response = session.Request(
            "POST",
            kTokenEndpoint,
            BuildNvidiaAuthHeaders({}, "application/x-www-form-urlencoded; charset=UTF-8"),
            body);
        const bool temporary = auth::IsTemporaryHttpStatus(response.status_code);
        if (response.status_code == 200 || !temporary || attempt == 3)
            break;
        AppendAuthLog(
            "auth: " + grant_name + " temporary failure HTTP " +
            std::to_string(response.status_code) + " retry=" + std::to_string(attempt));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(auth::RefreshRetryDelayMs(attempt)));
    }
    return response;
}

AuthTokens RefreshTokensWithClientTokenNative(const AuthSession& auth_session)
{
    if (auth_session.tokens.client_token.empty() || auth_session.user.user_id.empty())
        throw std::runtime_error("Saved session has no client-token refresh mechanism");

    NativeAuthHttpSession session;
    // Only warm login.nvidia.com. Following /authorize to accounts.nvgs is unnecessary
    // for token refresh and can fail against the older libnx certificate store.
    WarmNvidiaTokenConnection(session);
    const std::string body =
        "grant_type=" + UrlEncode("urn:ietf:params:oauth:grant-type:client_token", true) +
        "&client_token=" + UrlEncode(auth_session.tokens.client_token, true) +
        "&client_id=" + UrlEncode(kClientId, true) +
        "&sub=" + UrlEncode(auth_session.user.user_id, true);

    const NativeAuthResponse response = PostTokenGrant(session, body, "client-token refresh");
    if (response.status_code != 200)
    {
        AppendAuthLog(
            "auth: client-token refresh rejected HTTP " +
            std::to_string(response.status_code));
        if (response.status_code == 400 || response.status_code == 401)
            throw ReauthenticationRequired("Saved NVIDIA client token is no longer valid");
        throw std::runtime_error(
            "Client-token refresh failed with HTTP " + std::to_string(response.status_code));
    }

    JsonPtr root = LoadJson(response.body);
    AuthTokens tokens = MergeRefreshedTokens(ParseAuthTokens(root.get()), auth_session);
    if (tokens.client_token.empty() || IsNearExpiry(tokens.client_token_expires_at_ms))
        RequestClientToken(session, tokens);
    AppendAuthLog("auth: client-token refresh ok");
    return tokens;
}

AuthTokens RefreshTokensWithOAuthNative(const AuthSession& auth_session)
{
    if (auth_session.tokens.refresh_token.empty())
        throw std::runtime_error("Saved GeForce NOW session cannot be refreshed");

    NativeAuthHttpSession session;
    WarmNvidiaTokenConnection(session);
    const std::string body =
        "grant_type=refresh_token"
        "&refresh_token=" + UrlEncode(auth_session.tokens.refresh_token, true) +
        "&client_id=" + UrlEncode(kClientId, true);

    const NativeAuthResponse response = PostTokenGrant(session, body, "OAuth refresh");

    if (response.status_code != 200)
    {
        AppendAuthLog(
            "auth: native token refresh failed HTTP " + std::to_string(response.status_code));
        if (response.status_code == 400 || response.status_code == 401)
            throw ReauthenticationRequired(
                "Saved GeForce NOW login is no longer valid. Please sign in again.");
        throw std::runtime_error(
            "Could not refresh the saved login (HTTP " +
            std::to_string(response.status_code) + "). Please try again.");
    }

    JsonPtr root = LoadJson(response.body);
    AuthTokens tokens = MergeRefreshedTokens(ParseAuthTokens(root.get()), auth_session);
    if (tokens.client_token.empty() || IsNearExpiry(tokens.client_token_expires_at_ms))
        RequestClientToken(session, tokens);
    AppendAuthLog("auth: OAuth refresh ok");
    return tokens;
}

AuthTokens RefreshSessionTokensNative(const AuthSession& auth_session)
{
    const auto mechanism = auth::SelectRefreshMechanism(
        !auth_session.tokens.client_token.empty(),
        !auth_session.user.user_id.empty(),
        !auth_session.tokens.refresh_token.empty());

    const char* mechanism_name = "none";
    if (mechanism == auth::RefreshMechanism::ClientToken)
        mechanism_name = "client_token";
    else if (mechanism == auth::RefreshMechanism::OAuthRefreshToken)
        mechanism_name = "oauth_refresh_token";
    AppendAuthLog(
        "auth: refresh mechanism=" + std::string(mechanism_name) +
        " client=" +
            std::string(auth_session.tokens.client_token.empty() ? "missing" : "present") +
        " user=" + std::string(auth_session.user.user_id.empty() ? "missing" : "present") +
        " oauth=" +
            std::string(auth_session.tokens.refresh_token.empty() ? "missing" : "present"));

    if (mechanism == auth::RefreshMechanism::ClientToken)
    {
        try
        {
            return RefreshTokensWithClientTokenNative(auth_session);
        }
        catch (const ReauthenticationRequired& ex)
        {
            AppendAuthLog(
                "auth: client-token refresh rejected; OAuth fallback=" +
                std::string(auth_session.tokens.refresh_token.empty() ? "missing" : "available"));
            if (auth_session.tokens.refresh_token.empty())
                throw;
        }
        catch (const std::exception& ex)
        {
            AppendAuthLog(
                "auth: client-token refresh failed; OAuth fallback=" +
                std::string(auth_session.tokens.refresh_token.empty() ? "missing" : "available") +
                " error=" + ex.what());
            if (auth_session.tokens.refresh_token.empty())
                throw;
        }
    }

    if (!auth_session.tokens.refresh_token.empty())
        return RefreshTokensWithOAuthNative(auth_session);

    throw ReauthenticationRequired(
        "Saved GeForce NOW session has no usable refresh mechanism. Please sign in again.");
}

void RequestClientToken(NativeAuthHttpSession& session, AuthTokens& tokens)
{
    if (tokens.access_token.empty())
        return;
    const NativeAuthResponse response = session.Request(
        "GET", kClientTokenEndpoint, BuildNvidiaAuthHeaders(tokens.access_token));
    if (response.status_code != 200)
    {
        AppendAuthLog(
            "auth: client token request skipped HTTP " + std::to_string(response.status_code));
        return;
    }
    JsonPtr root = LoadJson(response.body);
    const std::string client_token = GetString(root.get(), "client_token");
    if (client_token.empty())
        return;
    tokens.client_token = client_token;
    tokens.client_token_expires_at_ms = ToExpiresAtMs(root.get(), "expires_in");
}

void RequestClientTokenNative(AuthTokens& tokens)
{
    NativeAuthHttpSession session;
    WarmNvidiaTokenConnection(session, tokens.access_token);
    RequestClientToken(session, tokens);
}

AuthUser FetchUserInfo(NativeAuthHttpSession& session, const AuthTokens& tokens)
{
    const NativeAuthResponse response = session.Request(
        "GET",
        kUserInfoEndpoint,
        BuildNvidiaAuthHeaders(tokens.access_token, {}, false, "application/json"));
    if (response.status_code != 200)
        throw std::runtime_error(
            "Failed to fetch user info with HTTP " + std::to_string(response.status_code));

    JsonPtr root = LoadJson(response.body);
    AuthUser user;
    user.user_id = GetString(root.get(), "sub");
    user.display_name = GetString(root.get(), "preferred_username");
    user.email = GetString(root.get(), "email");
    user.avatar_url = GetString(root.get(), "picture");
    // NVIDIA's user-info JWT does not carry the GeForce NOW subscription.
    // The authoritative tier is resolved separately through MES.
    user.membership_tier.clear();
    user.membership_tier_verified = false;
    if (user.display_name.empty() && !user.email.empty())
    {
        const auto at = user.email.find('@');
        user.display_name = at == std::string::npos ? user.email : user.email.substr(0, at);
    }
    if (user.display_name.empty())
        user.display_name = "GFN User";
    if (user.user_id.empty())
        throw std::runtime_error("Login succeeded but user info is incomplete");
    return user;
}

std::string ResolveSessionJwt(const AuthSession& session)
{
    // CloudMatch expects NVIDIA's signed ID token. OAuth access tokens may be opaque and
    // are rejected as GFNJWT before the session request is parsed.
    return session.tokens.id_token.empty() ? session.tokens.access_token : session.tokens.id_token;
}

std::string OptimizeImageUrl(const std::string& url, int width = 544)
{
    if (url.find("img.nvidiagrid.net") != std::string::npos)
    {
        std::string optimized = url;
        const auto width_marker = optimized.find(";w=");
        if (width_marker != std::string::npos)
        {
            auto width_end = width_marker + 3;
            while (width_end < optimized.size() &&
                   std::isdigit(static_cast<unsigned char>(optimized[width_end])))
            {
                ++width_end;
            }
            optimized.erase(width_marker, width_end - width_marker);
        }
        return optimized + ";w=" + std::to_string(width);
    }

    return url;
}

bool IsOwnedLibraryStatus(const std::string& status)
{
    return gfn::library::OwnedStatus(status);
}

bool IsNumericId(const std::string& value)
{
    if (value.empty())
        return false;

    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

void AppendUnique(std::vector<std::string>& values, std::string value)
{
    value = Trim(value);
    if (value.empty() ||
        std::find(values.begin(), values.end(), value) != values.end())
    {
        return;
    }
    values.push_back(std::move(value));
}

std::vector<std::string> ParseLabelArray(json_t* value)
{
    std::vector<std::string> labels;
    if (!json_is_array(value))
        return labels;

    size_t index = 0;
    json_t* entry = nullptr;
    json_array_foreach(value, index, entry)
    {
        if (json_is_string(entry) || json_is_integer(entry))
        {
            AppendUnique(labels, JsonString(entry));
            continue;
        }
        if (!json_is_object(entry))
            continue;

        for (const char* key :
             {"label", "name", "title", "displayName", "rating", "value"})
        {
            const std::string label = GetString(entry, key);
            if (!label.empty())
            {
                AppendUnique(labels, label);
                break;
            }
        }
    }
    return labels;
}

std::vector<std::string> ParseImageArray(json_t* images, const char* key, int width)
{
    std::vector<std::string> urls;
    if (!json_is_object(images))
        return urls;

    json_t* value = json_object_get(images, key);
    if (json_is_string(value))
    {
        AppendUnique(urls, OptimizeImageUrl(JsonString(value), width));
        return urls;
    }
    if (!json_is_array(value))
        return urls;

    size_t index = 0;
    json_t* entry = nullptr;
    json_array_foreach(value, index, entry)
    {
        if (json_is_string(entry))
            AppendUnique(urls, OptimizeImageUrl(JsonString(entry), width));
    }
    return urls;
}

GameInfo ParseApp(json_t* app)
{
    GameInfo game;
    game.id                 = GetString(app, "id");
    game.uuid               = game.id;
    game.title              = GetString(app, "title");
    game.description        = GetString(app, "longDescription");
    if (game.description.empty())
        game.description = GetString(app, "shortDescription");
    if (game.description.empty())
        game.description = GetString(app, "description");
    game.publisher          = GetString(app, "publisherName");
    game.developer          = GetString(app, "developerName");
    game.genres             = ParseLabelArray(json_object_get(app, "genres"));
    game.feature_labels     = ParseLabelArray(json_object_get(app, "featureLabels"));
    if (game.feature_labels.empty())
        game.feature_labels = ParseLabelArray(json_object_get(app, "features"));
    game.content_ratings    = ParseLabelArray(json_object_get(app, "contentRatings"));
    json_t* app_gfn = json_object_get(app, "gfn");
    game.membership_tier_label = GetString(app_gfn, "minimumMembershipTierLabel");
    json_t* app_library = app_gfn ? json_object_get(app_gfn, "library") : nullptr;
    game.last_played = GetString(app_library, "lastPlayedDate");
    if (game.last_played.empty())
        game.last_played = GetString(app_library, "lastPlayedAt");

    json_t* images = json_object_get(app, "images");
    const std::string key_art = GetString(images, "KEY_ART");
    const std::string box_art = GetString(images, "GAME_BOX_ART");
    const std::string tv_banner = GetString(images, "TV_BANNER");
    const std::string hero_image = GetString(images, "HERO_IMAGE");

    for (const std::string& candidate : {tv_banner, key_art, hero_image, box_art})
    {
        if (!candidate.empty())
        {
            game.image_url = OptimizeImageUrl(candidate, 544);
            break;
        }
    }
    for (const std::string& candidate : {box_art, key_art, tv_banner, hero_image})
    {
        if (!candidate.empty())
        {
            game.poster_url = OptimizeImageUrl(candidate, 720);
            break;
        }
    }
    for (const std::string& candidate : {hero_image, tv_banner, key_art})
    {
        if (!candidate.empty())
        {
            game.hero_url = OptimizeImageUrl(candidate, 1280);
            break;
        }
    }
    game.screenshots = ParseImageArray(images, "SCREENSHOTS", 1280);
    if (game.screenshots.empty())
        game.screenshots = ParseImageArray(images, "SCREENSHOT_THUMB", 1280);

    json_t* variants = json_object_get(app, "variants");
    if (json_is_array(variants))
    {
        size_t index = 0;
        json_t* entry = nullptr;
        json_array_foreach(variants, index, entry)
        {
            GameVariant variant;
            variant.id    = GetString(entry, "id");
            variant.store = GetString(entry, "appStore");
            for (const std::string& control :
                 ParseLabelArray(json_object_get(entry, "supportedControls")))
            {
                AppendUnique(game.supported_controls, control);
            }

            json_t* gfn = json_object_get(entry, "gfn");
            variant.gfn_status = GetString(gfn, "status");

            json_t* library = gfn ? json_object_get(gfn, "library") : nullptr;
            variant.library_status   = GetString(library, "status");
            variant.library_selected = GetBool(library, "selected");
            variant.last_played_date = GetString(library, "lastPlayedDate");
            if (variant.last_played_date.empty())
                variant.last_played_date = GetString(library, "lastPlayedAt");
            if (variant.last_played_date.empty())
                variant.last_played_date = GetString(library, "lastPlayed");

            if (variant.library_selected) {
                game.selected_variant_index = index;
                game.launch_app_id = variant.id;
            }

            if (!variant.last_played_date.empty() &&
                (game.last_played.empty() || variant.last_played_date > game.last_played))
                game.last_played = variant.last_played_date;

            if (!variant.store.empty() &&
                std::find(game.available_stores.begin(), game.available_stores.end(), variant.store) ==
                    game.available_stores.end())
            {
                game.available_stores.push_back(variant.store);
            }

            if (IsOwnedLibraryStatus(variant.library_status))
                game.is_in_library = true;

            if (game.launch_app_id.empty() && IsNumericId(variant.id))
                game.launch_app_id = variant.id;

            game.variants.push_back(std::move(variant));
        }
    }

    if (game.launch_app_id.empty() && IsNumericId(game.id))
        game.launch_app_id = game.id;

    return game;
}

void MergeGame(GameInfo& target, const GameInfo& incoming)
{
    if (target.image_url.empty())
        target.image_url = incoming.image_url;

    if (target.description.empty())
        target.description = incoming.description;

    if (target.publisher.empty())
        target.publisher = incoming.publisher;

    if (target.developer.empty())
        target.developer = incoming.developer;

    if (target.poster_url.empty())
        target.poster_url = incoming.poster_url;

    if (target.hero_url.empty())
        target.hero_url = incoming.hero_url;

    for (const auto& screenshot : incoming.screenshots)
    {
        if (std::find(target.screenshots.begin(), target.screenshots.end(), screenshot) ==
            target.screenshots.end())
        {
            target.screenshots.push_back(screenshot);
        }
    }

    for (const auto& genre : incoming.genres)
        AppendUnique(target.genres, genre);
    for (const auto& feature : incoming.feature_labels)
        AppendUnique(target.feature_labels, feature);
    for (const auto& rating : incoming.content_ratings)
        AppendUnique(target.content_ratings, rating);
    for (const auto& control : incoming.supported_controls)
        AppendUnique(target.supported_controls, control);

    if (!incoming.last_played.empty() &&
        (target.last_played.empty() || incoming.last_played > target.last_played))
        target.last_played = incoming.last_played;

    if (target.launch_app_id.empty())
        target.launch_app_id = incoming.launch_app_id;

    if (target.membership_tier_label.empty())
        target.membership_tier_label = incoming.membership_tier_label;

    target.is_in_library = target.is_in_library || incoming.is_in_library;

    for (const auto& store : incoming.available_stores)
    {
        if (std::find(target.available_stores.begin(), target.available_stores.end(), store) ==
            target.available_stores.end())
        {
            target.available_stores.push_back(store);
        }
    }

    for (const auto& variant : incoming.variants)
    {
        const auto it = std::find_if(
            target.variants.begin(),
            target.variants.end(),
            [&variant](const GameVariant& existing) { return existing.id == variant.id; });

        if (it == target.variants.end())
            target.variants.push_back(variant);
    }
}

void ThrowIfGraphQlFailed(json_t* root)
{
    json_t* errors = root ? json_object_get(root, "errors") : nullptr;
    if (!json_is_array(errors) || json_array_size(errors) == 0)
        return;

    std::string message = "GFN GraphQL returned errors";
    json_t* first       = json_array_get(errors, 0);
    const std::string first_message = GetString(first, "message");
    if (!first_message.empty())
        message += ": " + first_message;

    throw std::runtime_error(message);
}

std::string BuildLibraryUrl(const std::string& vpc_id, bool with_library_time)
{
    const std::string variables =
        std::string("{\"vpcId\":\"") + vpc_id + "\",\"locale\":\"" + kDefaultLocale +
        "\",\"panelNames\":[\"LIBRARY\"]}";

    const std::string extensions =
        std::string("{\"persistedQuery\":{\"sha256Hash\":\"") +
        (with_library_time ? kLibraryWithTimeQueryHash : kPanelsQueryHash) + "\"}}";

    std::string url = std::string(kGraphQlEndpoint) + "?";
    url += "requestType=" + UrlEncode("panels/Library");
    url += "&extensions=" + UrlEncode(extensions);
    url += "&huId=" + UrlEncode(HexEncode(GenerateRandomBytes(8).data(), 8));
    url += "&variables=" + UrlEncode(variables);
    return url;
}

std::string BuildAppMetadataUrl(
    const std::string& vpc_id, const std::vector<std::string>& app_ids)
{
    JsonPtr variables(json_object(), &json_decref);
    JsonPtr ids(json_array(), &json_decref);
    json_object_set_new(variables.get(), "vpcId", json_string(vpc_id.c_str()));
    json_object_set_new(variables.get(), "locale", json_string(kDefaultLocale));
    for (const std::string& id : app_ids)
        json_array_append_new(ids.get(), json_string(id.c_str()));
    json_object_set_new(variables.get(), "appIds", json_incref(ids.get()));

    JsonPtr extensions(json_object(), &json_decref);
    JsonPtr persisted(json_object(), &json_decref);
    json_object_set_new(
        persisted.get(), "sha256Hash", json_string(kAppMetadataQueryHash));
    json_object_set_new(
        extensions.get(), "persistedQuery", json_incref(persisted.get()));

    std::string url = std::string(kGraphQlEndpoint) + "?";
    url += "requestType=" + UrlEncode("appMetaData");
    url += "&extensions=" + UrlEncode(DumpJson(extensions.get()));
    const std::vector<unsigned char> random = GenerateRandomBytes(8);
    url += "&huId=" + UrlEncode(HexEncode(random.data(), random.size()));
    url += "&variables=" + UrlEncode(DumpJson(variables.get()));
    return url;
}

std::string ResolveVpcId(const HttpClient& http_client, const AuthSession& session)
{
    const std::string token = ResolveSessionJwt(session);
    if (token.empty())
        return "NP-AMS-08";

    const StreamSettings settings = LoadStreamSettings();
    const std::string base_url = server_location::ResolveStreamingBaseUrl(
        settings.region, session.provider.streaming_service_url);
    if (base_url.empty())
        return "NP-AMS-08";

    std::vector<std::string> headers = BuildMembershipHeaders(token);
    for (std::string& header : headers)
    {
        if (header == "nv-client-type: NATIVE")
            header = "nv-client-type: BROWSER";
        else if (header == "nv-client-streamer: NVIDIA-CLASSIC")
            header = "nv-client-streamer: WEBRTC";
    }
    const HttpResponse response = http_client.Get(
        EnsureTrailingSlash(base_url) + "v2/serverInfo",
        GfnClient::kUserAgent,
        headers);

    if (response.status_code != 200)
        return "NP-AMS-08";

    try
    {
        JsonPtr root = LoadJson(response.body);
        json_t* request_status = json_object_get(root.get(), "requestStatus");
        const std::string server_id = GetString(request_status, "serverId");
        return server_id.empty() ? std::string("NP-AMS-08") : server_id;
    }
    catch (...)
    {
        return "NP-AMS-08";
    }
}

bool RefreshMembershipTier(const HttpClient& http_client, AuthSession& session)
{
    session.membership_checked_at_ms = NowMs();
    const std::string token = ResolveSessionJwt(session);
    if (token.empty() || session.user.user_id.empty())
    {
        AppendAuthLog("auth: membership lookup skipped because identity is incomplete");
        return false;
    }

    try
    {
        const std::string vpc_id = ResolveVpcId(http_client, session);
        const std::string url = std::string(kSubscriptionEndpoint) +
            "?serviceName=gfn_pc&languageCode=en_US&vpcId=" + UrlEncode(vpc_id) +
            "&userId=" + UrlEncode(session.user.user_id);
        const HttpResponse response = http_client.Get(
            url,
            GfnClient::kUserAgent,
            BuildMembershipHeaders(token));
        if (response.status_code != 200)
        {
            AppendAuthLog(
                "auth: membership lookup failed HTTP=" +
                std::to_string(response.status_code) +
                " keepingVerified=" +
                std::to_string(session.user.membership_tier_verified ? 1 : 0));
            return false;
        }

        JsonPtr root = LoadJson(response.body);
        const SubscriptionInfo subscription = ParseSubscription(root.get());
        const std::string tier = subscription.membership_tier;
        if (tier.empty())
        {
            AppendAuthLog("auth: membership lookup returned no membershipTier");
            return false;
        }

        session.subscription = subscription;
        session.user.membership_tier = tier;
        session.user.membership_tier_verified = true;
        AppendAuthLog(
            "auth: membership tier verified tier=" + tier +
            " vpc=" + vpc_id +
            " remainingHours=" + std::to_string(session.subscription.remaining_hours) +
            " storage=" + std::to_string(session.subscription.storage_size_gb));
        return true;
    }
    catch (const std::exception& e)
    {
        AppendAuthLog(
            "auth: membership lookup failed error=" + std::string(e.what()) +
            " keepingVerified=" +
            std::to_string(session.user.membership_tier_verified ? 1 : 0));
        return false;
    }
}

std::vector<GameInfo> ParseLibraryGames(JsonPtr& root)
{
    ThrowIfGraphQlFailed(root.get());

    std::unordered_map<std::string, size_t> index_by_id;
    std::vector<GameInfo> games;

    json_t* data   = json_object_get(root.get(), "data");
    json_t* panels = data ? json_object_get(data, "panels") : nullptr;
    if (!json_is_array(panels))
        return {};

    size_t panel_index = 0;
    json_t* panel      = nullptr;
    json_array_foreach(panels, panel_index, panel)
    {
        json_t* sections = json_object_get(panel, "sections");
        if (!json_is_array(sections))
            continue;

        size_t section_index = 0;
        json_t* section      = nullptr;
        json_array_foreach(sections, section_index, section)
        {
            json_t* items = json_object_get(section, "items");
            if (!json_is_array(items))
                continue;

            size_t item_index = 0;
            json_t* item      = nullptr;
            json_array_foreach(items, item_index, item)
            {
                if (GetString(item, "__typename") != "GameItem")
                    continue;

                json_t* app = json_object_get(item, "app");
                if (!json_is_object(app))
                    continue;

                GameInfo game = ParseApp(app);
                if (game.id.empty() || game.title.empty())
                    continue;

                const auto existing = index_by_id.find(game.id);
                if (existing == index_by_id.end())
                {
                    index_by_id.emplace(game.id, games.size());
                    games.push_back(std::move(game));
                }
                else
                {
                    MergeGame(games[existing->second], game);
                }
            }
        }
    }

    return games;
}

void EnrichGamesWithMetadata(
    const HttpClient& http_client,
    const std::string& jwt_token,
    const std::string& vpc_id,
    std::vector<GameInfo>& games)
{
    std::vector<std::string> app_ids;
    app_ids.reserve(games.size());
    for (const GameInfo& game : games)
    {
        const std::string& id = game.uuid.empty() ? game.id : game.uuid;
        if (!id.empty() &&
            std::find(app_ids.begin(), app_ids.end(), id) == app_ids.end())
        {
            app_ids.push_back(id);
        }
    }
    if (app_ids.empty())
        return;

    std::unordered_map<std::string, size_t> game_by_id;
    for (size_t index = 0; index < games.size(); ++index)
    {
        game_by_id[games[index].id] = index;
        if (!games[index].uuid.empty())
            game_by_id[games[index].uuid] = index;
    }

    constexpr size_t kChunkSize = 40;
    for (size_t offset = 0; offset < app_ids.size(); offset += kChunkSize)
    {
        const size_t end = std::min(offset + kChunkSize, app_ids.size());
        const std::vector<std::string> chunk(
            app_ids.begin() + static_cast<std::ptrdiff_t>(offset),
            app_ids.begin() + static_cast<std::ptrdiff_t>(end));

        const HttpResponse response = http_client.Get(
            BuildAppMetadataUrl(vpc_id, chunk),
            GfnClient::kUserAgent,
            BuildGraphQlHeaders(jwt_token));
        if (response.status_code != 200)
        {
            throw std::runtime_error(
                "App metadata fetch failed with HTTP " +
                std::to_string(response.status_code));
        }

        JsonPtr root = LoadJson(response.body);
        ThrowIfGraphQlFailed(root.get());
        json_t* data = json_object_get(root.get(), "data");
        json_t* apps = data ? json_object_get(data, "apps") : nullptr;
        json_t* items = apps ? json_object_get(apps, "items") : nullptr;
        if (!json_is_array(items))
            continue;

        size_t item_index = 0;
        json_t* item = nullptr;
        json_array_foreach(items, item_index, item)
        {
            GameInfo metadata = ParseApp(item);
            const auto target = game_by_id.find(metadata.id);
            if (target != game_by_id.end())
                MergeGame(games[target->second], metadata);
        }
    }
}

PublicGame ToPublicGame(const GameInfo& source)
{
    PublicGame game;
    game.id                    = source.id;
    game.uuid                  = source.uuid;
    game.launch_app_id         = source.launch_app_id;
    game.title                 = source.title;
    game.publisher             = source.publisher;
    game.developer             = source.developer;
    game.description           = source.description;
    game.image_url             = source.image_url;
    game.poster_url            = source.poster_url;
    game.hero_url              = source.hero_url;
    game.screenshots           = source.screenshots;
    game.genres                = source.genres;
    game.feature_labels        = source.feature_labels;
    game.content_ratings       = source.content_ratings;
    game.supported_controls    = source.supported_controls;
    game.membership_tier_label = source.membership_tier_label;
    game.is_in_library         = source.is_in_library;
    game.variants              = source.variants;

    if (source.selected_variant_index < source.variants.size())
        game.store = source.variants[source.selected_variant_index].store;
    if (game.store.empty() && !source.available_stores.empty())
        game.store = source.available_stores.front();
    if (game.store.empty())
        game.store = "Unknown";
    return game;
}

std::vector<PublicGame> ParseCatalogPage(
    JsonPtr& root, bool& has_next_page, std::string& end_cursor)
{
    ThrowIfGraphQlFailed(root.get());
    has_next_page = false;
    end_cursor.clear();

    json_t* data = json_object_get(root.get(), "data");
    json_t* apps = data ? json_object_get(data, "apps") : nullptr;
    json_t* items = apps ? json_object_get(apps, "items") : nullptr;
    if (!json_is_array(items))
        return {};

    json_t* page_info = json_object_get(apps, "pageInfo");
    has_next_page = GetBool(page_info, "hasNextPage");
    end_cursor = GetString(page_info, "endCursor");

    std::vector<PublicGame> games;
    size_t index = 0;
    json_t* app = nullptr;
    json_array_foreach(items, index, app)
    {
        GameInfo parsed = ParseApp(app);
        if (!parsed.id.empty() && !parsed.title.empty())
            games.push_back(ToPublicGame(parsed));
    }
    return games;
}

std::string BuildCatalogRequestBody(
    const std::string& vpc_id, const std::string& search_query,
    const std::string& cursor)
{
    static const char* kBrowseQuery = R"GRAPHQL(
query GetFilterBrowseResults($vpcId: String!, $locale: String!, $sortString: String!, $fetchCount: Int!, $cursor: String!, $filters: AppFilterFields!) {
  apps(vpcId: $vpcId, language: $locale, orderBy: $sortString, first: $fetchCount, after: $cursor, filters: $filters) {
    numberReturned numberSupported
    pageInfo { hasNextPage endCursor totalCount }
    items {
      id title longDescription shortDescription publisherName developerName genres
      images { KEY_ART GAME_BOX_ART TV_BANNER HERO_IMAGE SCREENSHOTS SCREENSHOT_THUMB }
      variants { id appStore supportedControls gfn { status library { status selected } } }
      gfn { playabilityState minimumMembershipTierLabel }
    }
  }
})GRAPHQL";
    static const char* kSearchQuery = R"GRAPHQL(
query GetSearchFilterResults($vpcId: String!, $locale: String!, $sortString: String!, $fetchCount: Int!, $cursor: String!, $searchString: String!, $filters: AppFilterFields!) {
  apps(vpcId: $vpcId, language: $locale, orderBy: $sortString, first: $fetchCount, after: $cursor, searchQuery: $searchString, filters: $filters) {
    numberReturned numberSupported
    pageInfo { hasNextPage endCursor totalCount }
    items {
      id title longDescription shortDescription publisherName developerName genres
      images { KEY_ART GAME_BOX_ART TV_BANNER HERO_IMAGE SCREENSHOTS SCREENSHOT_THUMB }
      variants { id appStore supportedControls gfn { status library { status selected } } }
      gfn { playabilityState minimumMembershipTierLabel }
    }
  }
})GRAPHQL";

    JsonPtr body(json_object(), &json_decref);
    JsonPtr variables(json_object(), &json_decref);
    json_object_set_new(body.get(), "query", json_string(search_query.empty() ? kBrowseQuery : kSearchQuery));
    json_object_set_new(variables.get(), "vpcId", json_string(vpc_id.c_str()));
    json_object_set_new(variables.get(), "locale", json_string(kDefaultLocale));
    json_object_set_new(
        variables.get(), "sortString",
        json_string("itemMetadata.relevance:DESC,sortName:ASC"));
    json_object_set_new(variables.get(), "fetchCount", json_integer(120));
    json_object_set_new(variables.get(), "cursor", json_string(cursor.c_str()));
    json_object_set_new(variables.get(), "filters", json_object());
    if (!search_query.empty())
        json_object_set_new(variables.get(), "searchString", json_string(search_query.c_str()));
    json_object_set_new(body.get(), "variables", json_incref(variables.get()));
    return DumpJson(body.get());
}

std::vector<std::string> BuildGraphQlPostHeaders(const std::string& token)
{
    std::vector<std::string> headers = BuildGraphQlHeaders(token);
    for (std::string& header : headers)
    {
        if (header.find("Content-Type:") == 0)
            header = "Content-Type: application/json";
    }
    return headers;
}

std::string InferStore(json_t* item)
{
    const std::string explicit_store = GetString(item, "store");
    if (!explicit_store.empty())
        return explicit_store;

    const std::string publisher = GetString(item, "publisher");
    if (publisher.find("NCSoft") != std::string::npos ||
        publisher.find("ncsoft") != std::string::npos)
    {
        return "NCSoft";
    }

    return "Unknown";
}

std::string BuildSteamImageUrl(const std::string& steam_url)
{
    const std::string marker = "/app/";
    const auto marker_pos    = steam_url.find(marker);
    if (marker_pos == std::string::npos)
        return "";

    const auto id_begin = marker_pos + marker.size();
    const auto id_end   = steam_url.find('/', id_begin);
    const std::string steam_id =
        steam_url.substr(id_begin, id_end == std::string::npos ? std::string::npos : id_end - id_begin);

    if (steam_id.empty())
        return "";

    return "https://cdn.cloudflare.steamstatic.com/steam/apps/" + steam_id + "/library_600x900.jpg";
}

} // namespace

#include "gfn/catalog.inc"
#include "gfn/auth.inc"
#include "gfn/library.inc"
#include "gfn/session.inc"
#include "gfn/session_recovery.inc"

} // namespace opennow
