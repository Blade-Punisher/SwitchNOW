#include "stream_settings.hpp"
#include "proxy_policy.hpp"
#include "app_paths.hpp"
#include "atomic_file_replace.hpp"
#include "localization.hpp"
#include "server_location_policy.hpp"
#include "stream_adaptation_policy.hpp"

#include <jansson.h>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

namespace opennow
{
namespace
{

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

std::string GetAppHome()
{
    return AppHomePath();
}

std::string GetSettingsPath()
{
    return GetAppHome() + "/stream_settings.json";
}

void EnsureAppHome()
{
    PrepareAppStorage();
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

std::string JsonString(json_t* value)
{
    if (!json_is_string(value))
        return "";

    const char* raw = json_string_value(value);
    return raw ? raw : "";
}

int JsonInt(json_t* object, const char* key, int fallback)
{
    if (!object || !json_is_object(object))
        return fallback;

    json_t* value = json_object_get(object, key);
    return json_is_integer(value) ? static_cast<int>(json_integer_value(value)) : fallback;
}

bool JsonBool(json_t* object, const char* key, bool fallback)
{
    if (!object || !json_is_object(object))
        return fallback;
    json_t* value = json_object_get(object, key);
    return json_is_boolean(value) ? json_is_true(value) : fallback;
}

std::string JsonField(json_t* object, const char* key, const std::string& fallback)
{
    if (!object || !json_is_object(object))
        return fallback;

    const std::string value = JsonString(json_object_get(object, key));
    return value.empty() ? fallback : value;
}

StreamSettings Sanitize(StreamSettings settings)
{
    if (settings.width <= 0 || settings.height <= 0 || settings.fps <= 0 || settings.bitrate_kbps <= 0)
        return StreamPresets().front();

    if (settings.codec != "H264")
        settings.codec = "H264";

    if (server_location::IsClosest(settings.region))
        settings.region = "Closest";
    else if (server_location::IsAutomatic(settings.region))
        settings.region = "Auto";
    else
    {
        settings.region =
            server_location::NormalizeStreamingBaseUrl(settings.region);
        if (settings.region.empty())
            settings.region = "Auto";
    }

    settings.audio_volume = std::max(100, std::min(800, settings.audio_volume));
    settings.audio_buffer_ms = std::max(30, std::min(100, settings.audio_buffer_ms));
    if (settings.video_backend != "Auto" && settings.video_backend != "NVDEC" &&
        settings.video_backend != "Software")
        settings.video_backend = "Auto";

    const auto& languages = GameLanguageOptions();
    const bool language_supported = std::any_of(
        languages.begin(), languages.end(), [&settings](const GameLanguageOption& option) {
            return option.code == settings.game_language;
        });
    if (!language_supported)
        settings.game_language = "en_US";

    if (settings.controller_layout != "Xbox" && settings.controller_layout != "Switch")
        settings.controller_layout = "Xbox";

    settings.image_quality_mode = streaming::NormalizeMode(settings.image_quality_mode);

    if (!IsSupportedInterfaceLanguage(settings.interface_language))
        settings.interface_language = "en";

    settings.proxy_url = proxy::NormalizeUrl(settings.proxy_url);
    if (!proxy::ValidCredential(settings.proxy_username)) settings.proxy_username.clear();
    if (!proxy::ValidCredential(settings.proxy_password)) settings.proxy_password.clear();
    if (settings.proxy_url.empty()) settings.proxy_enabled = false;
    return settings;
}

StreamSettings MakePreset(
    const char* id, const char* label, int width, int height, int fps, int bitrate_kbps)
{
    StreamSettings settings;
    settings.preset_id = id;
    settings.label = label;
    settings.width = width;
    settings.height = height;
    settings.fps = fps;
    settings.bitrate_kbps = bitrate_kbps;
    return settings;
}

} // namespace

const std::vector<StreamSettings>& StreamPresets()
{
    static const std::vector<StreamSettings> presets = {
        MakePreset("safe", "Safe", 1280, 720, 30, 8000),
        MakePreset("balanced", "Balanced", 1280, 720, 60, 12000),
        MakePreset("quality", "Quality", 1920, 1080, 60, 20000),
    };
    return presets;
}

const std::vector<GameLanguageOption>& GameLanguageOptions()
{
    // Keep this list aligned with OpenNOW's GameLanguage union and settings UI.
    static const std::vector<GameLanguageOption> languages = {
        {"en_US", "English (US)"},
        {"en_GB", "English (UK)"},
        {"de_DE", "German"},
        {"fr_FR", "French"},
        {"es_ES", "Spanish (Spain)"},
        {"es_MX", "Spanish (Latin America)"},
        {"it_IT", "Italian"},
        {"pt_PT", "Portuguese (Portugal)"},
        {"pt_BR", "Portuguese (Brazil)"},
        {"ru_RU", "Russian"},
        {"pl_PL", "Polish"},
        {"tr_TR", "Turkish"},
        {"ar_SA", "Arabic"},
        {"ja_JP", "Japanese"},
        {"ko_KR", "Korean"},
        {"zh_CN", "Chinese (Simplified)"},
        {"zh_TW", "Chinese (Traditional)"},
        {"th_TH", "Thai"},
        {"vi_VN", "Vietnamese"},
        {"id_ID", "Indonesian"},
        {"cs_CZ", "Czech"},
        {"el_GR", "Greek"},
        {"hu_HU", "Hungarian"},
        {"ro_RO", "Romanian"},
        {"uk_UA", "Ukrainian"},
        {"nl_NL", "Dutch"},
        {"sv_SE", "Swedish"},
        {"da_DK", "Danish"},
        {"fi_FI", "Finnish"},
        {"no_NO", "Norwegian"},
    };
    return languages;
}

std::string GameLanguageLabel(const std::string& code)
{
    const auto& languages = GameLanguageOptions();
    const auto found = std::find_if(
        languages.begin(), languages.end(), [&code](const GameLanguageOption& option) {
            return option.code == code;
        });
    return found == languages.end() ? std::string("English (US)") : found->label;
}

StreamSettings LoadStreamSettings()
{
    const std::string body = ReadTextFile(GetSettingsPath());
    if (body.empty())
        return StreamPresets()[1];

    json_error_t error {};
    JsonPtr root(json_loads(body.c_str(), 0, &error), &json_decref);
    if (!root || !json_is_object(root.get()))
        return StreamPresets()[1];

    StreamSettings settings;
    settings.preset_id    = JsonField(root.get(), "preset_id", settings.preset_id);
    settings.label        = JsonField(root.get(), "label", settings.label);
    settings.width        = JsonInt(root.get(), "width", settings.width);
    settings.height       = JsonInt(root.get(), "height", settings.height);
    settings.fps          = JsonInt(root.get(), "fps", settings.fps);
    settings.bitrate_kbps = JsonInt(root.get(), "bitrate_kbps", settings.bitrate_kbps);
    settings.codec        = JsonField(root.get(), "codec", settings.codec);
    settings.region       = JsonField(root.get(), "region", settings.region);
    settings.audio_enabled = JsonBool(root.get(), "audio_enabled", settings.audio_enabled);
    settings.audio_volume = JsonInt(root.get(), "audio_volume", settings.audio_volume);
    const int gain_version = JsonInt(root.get(), "audio_gain_version", 0);
    // The old 25-150% scale only reached +3.5 dB. Preserve its relative
    // position while migrating to the useful 1x-8x boost range.
    if (gain_version < 3 && settings.audio_volume > 0 && settings.audio_volume <= 150)
        settings.audio_volume = std::max(100, std::min(800, settings.audio_volume * 4));
    // Version 4 raises the complete range because NVIDIA's decoded PCM is
    // unusually quiet. The previous 6x default becomes the new 12x default.
    if (gain_version < 4 && settings.audio_volume <= 800)
        settings.audio_volume = std::max(800, std::min(1600, settings.audio_volume * 2));
    // R6 restores headroom for the limiter. Preserve the user's relative
    // setting while moving the previous 8x-16x scale to 4x-8x.
    if (gain_version < 5)
        settings.audio_volume = std::max(100, std::min(800, settings.audio_volume / 2));
    settings.audio_buffer_ms = JsonInt(root.get(), "audio_buffer_ms", settings.audio_buffer_ms);
    settings.video_backend = JsonField(root.get(), "video_backend", settings.video_backend);
    settings.debug_diagnostics = JsonBool(root.get(), "debug_diagnostics", false);
    settings.stats_overlay_enabled = JsonBool(root.get(), "stats_overlay_enabled", false);
    settings.game_language = JsonField(root.get(), "game_language", settings.game_language);
    settings.persist_game_settings = JsonBool(
        root.get(), "persist_game_settings", settings.persist_game_settings);
    settings.controller_layout = JsonField(
        root.get(), "controller_layout", settings.controller_layout);
    settings.image_quality_mode = JsonField(
        root.get(), "image_quality_mode", settings.image_quality_mode);
    settings.interface_language = JsonField(
        root.get(), "interface_language", settings.interface_language);
    settings.proxy_enabled = JsonBool(
        root.get(), "proxy_enabled", settings.proxy_enabled);
    settings.proxy_url = JsonField(
        root.get(), "proxy_url", settings.proxy_url);
    settings.proxy_username = JsonField(root.get(), "proxy_username", {});
    settings.proxy_password = JsonField(root.get(), "proxy_password", {});
    return Sanitize(settings);
}

bool SaveStreamSettings(const StreamSettings& settings)
{
    EnsureAppHome();
    const StreamSettings clean = Sanitize(settings);

    JsonPtr root(json_object(), &json_decref);
    json_object_set_new(root.get(), "preset_id", json_string(clean.preset_id.c_str()));
    json_object_set_new(root.get(), "label", json_string(clean.label.c_str()));
    json_object_set_new(root.get(), "width", json_integer(clean.width));
    json_object_set_new(root.get(), "height", json_integer(clean.height));
    json_object_set_new(root.get(), "fps", json_integer(clean.fps));
    json_object_set_new(root.get(), "bitrate_kbps", json_integer(clean.bitrate_kbps));
    json_object_set_new(root.get(), "codec", json_string(clean.codec.c_str()));
    json_object_set_new(root.get(), "region", json_string(clean.region.c_str()));
    json_object_set_new(root.get(), "audio_enabled", json_boolean(clean.audio_enabled));
    json_object_set_new(root.get(), "audio_volume", json_integer(clean.audio_volume));
    json_object_set_new(root.get(), "audio_gain_version", json_integer(5));
    json_object_set_new(root.get(), "audio_buffer_ms", json_integer(clean.audio_buffer_ms));
    json_object_set_new(root.get(), "video_backend", json_string(clean.video_backend.c_str()));
    json_object_set_new(root.get(), "debug_diagnostics", json_boolean(clean.debug_diagnostics));
    json_object_set_new(root.get(), "stats_overlay_enabled", json_boolean(clean.stats_overlay_enabled));
    json_object_set_new(root.get(), "game_language", json_string(clean.game_language.c_str()));
    json_object_set_new(
        root.get(), "persist_game_settings", json_boolean(clean.persist_game_settings));
    json_object_set_new(
        root.get(), "controller_layout", json_string(clean.controller_layout.c_str()));
    json_object_set_new(
        root.get(), "image_quality_mode", json_string(clean.image_quality_mode.c_str()));
    json_object_set_new(
        root.get(), "interface_language", json_string(clean.interface_language.c_str()));
    json_object_set_new(
        root.get(), "proxy_enabled", json_boolean(clean.proxy_enabled));
    json_object_set_new(
        root.get(), "proxy_url", json_string(clean.proxy_url.c_str()));
    json_object_set_new(root.get(), "proxy_username", json_string(clean.proxy_username.c_str()));
    json_object_set_new(root.get(), "proxy_password", json_string(clean.proxy_password.c_str()));

    char* dump = json_dumps(root.get(), JSON_INDENT(2));
    if (!dump)
        return false;

    const std::string path = GetSettingsPath();
    const std::string temporary_path = path + ".tmp";
    std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        std::free(dump);
        return false;
    }

    stream.write(dump, static_cast<std::streamsize>(std::strlen(dump)));
    stream.flush();
    const bool write_ok = stream.good();
    stream.close();

    std::free(dump);
    if (!write_ok)
    {
        std::remove(temporary_path.c_str());
        return false;
    }

    return storage::ReplaceWithTemporaryFile(temporary_path, path);
}

StreamSettings NextStreamPreset(const StreamSettings& current)
{
    const auto& presets = StreamPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (presets[i].preset_id == current.preset_id)
            return presets[(i + 1) % presets.size()];
    }

    return presets.front();
}

std::string FormatStreamSettings(const StreamSettings& settings)
{
    return settings.label + " | " + std::to_string(settings.width) + "x" +
           std::to_string(settings.height) + " @ " + std::to_string(settings.fps) +
           " FPS | " + std::to_string(settings.bitrate_kbps / 1000) + " Mbps | " +
           settings.codec + " | Backend: " + settings.video_backend +
           " | Region: " + settings.region +
           " | Game language: " + settings.game_language +
           " | Save game graphics: " +
           (settings.persist_game_settings ? "On" : "Off") +
           " | Controls: " + settings.controller_layout +
           " | Motion quality: " + settings.image_quality_mode;
           
}

} // namespace opennow
